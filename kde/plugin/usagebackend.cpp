#include "usagebackend.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtMath>

#include <unistd.h>

namespace {
void wipe(QByteArray &value)
{
    value.fill('\0');
    value.clear();
}
}

UsageBackend::UsageBackend(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.setStandardErrorFile(QProcess::nullDevice());
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(10'000);
    m_refreshTimer.setInterval(5 * 60 * 1000);

    connect(&m_refreshTimer, &QTimer::timeout, this, &UsageBackend::refresh);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &UsageBackend::consumeOutput);
    connect(&m_process, &QProcess::stateChanged, this, &UsageBackend::busyChanged);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_enabled) {
            wipe(m_output);
            finishWithError(QStringLiteral("Could not start Codex"));
        }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        m_timeout.stop();
        if (m_enabled && !m_receivedReply && m_status == QStringLiteral("Refreshing…")) {
            wipe(m_output);
            finishWithError(QStringLiteral("Codex did not return rate limits"));
        }
    });
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_enabled)
            return;
        finishWithError(QStringLiteral("Codex timed out"));
        wipe(m_output);
        stopProcess();
    });
}

int UsageBackend::percent() const
{
    if (m_windows.isEmpty())
        return 0;
    return m_windows.first().toMap().value(QStringLiteral("percent")).toInt();
}

void UsageBackend::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;
    ++m_generation;
    Q_EMIT enabledChanged();

    if (!enabled) {
        m_refreshTimer.stop();
        m_timeout.stop();
        stopProcess();
        wipe(m_output);
        m_windows.clear();
        Q_EMIT readingChanged();
        setStatus(QStringLiteral("Disabled"));
        return;
    }

    m_refreshTimer.start();
    refresh();
}

void UsageBackend::refresh()
{
    if (!m_enabled || m_process.state() != QProcess::NotRunning)
        return;

    const QString executable = findCodex();
    if (executable.isEmpty()) {
        finishWithError(QStringLiteral("Trusted Codex executable not found"));
        return;
    }

    wipe(m_output);
    m_receivedReply = false;
    setStatus(QStringLiteral("Refreshing…"));
    const quint64 generation = m_generation;
    m_process.setProgram(executable);
    m_process.setArguments({QStringLiteral("app-server")});
    m_process.start(QIODevice::ReadWrite);
    if (!m_process.waitForStarted(1000))
        return;

    if (!m_enabled || generation != m_generation) {
        stopProcess();
        return;
    }

    static const QByteArray request =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
        "\"params\":{\"clientInfo\":{\"name\":\"codenotch-kde\",\"version\":\"0.1\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"account/rateLimits/read\",\"params\":null}\n";
    m_process.write(request);
    m_timeout.start();
}

void UsageBackend::consumeOutput()
{
    if (!m_enabled) {
        m_process.readAllStandardOutput();
        return;
    }

    m_output += m_process.readAllStandardOutput();
    if (m_output.size() > MaxReplyBytes) {
        finishWithError(QStringLiteral("Codex reply was too large"));
        wipe(m_output);
        stopProcess();
        return;
    }

    qsizetype newline = -1;
    while ((newline = m_output.indexOf('\n')) >= 0) {
        const QByteArray line = m_output.left(newline);
        m_output.remove(0, newline + 1);

        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject() || document.object().value(QStringLiteral("id")).toInt(-1) != 2)
            continue;

        const QVariantList parsed = parseRateLimits(line);
        if (parsed.isEmpty()) {
            wipe(m_output);
            stopProcess();
            finishWithError(QStringLiteral("Codex returned no valid rate limits"));
            return;
        }

        m_windows = parsed;
        m_receivedReply = true;
        m_timeout.stop();
        stopProcess();
        wipe(m_output);
        Q_EMIT readingChanged();
        setStatus(QStringLiteral("Up to date"));
        return;
    }
}

QVariantList UsageBackend::parseRateLimits(const QByteArray &line)
{
    const QJsonObject root = QJsonDocument::fromJson(line).object();
    if (root.value(QStringLiteral("id")).toInt(-1) != 2)
        return {};

    const QJsonObject limits = root.value(QStringLiteral("result")).toObject()
                                   .value(QStringLiteral("rateLimits")).toObject();
    QVariantList result;
    for (const QString &id : {QStringLiteral("primary"), QStringLiteral("secondary")}) {
        const QJsonObject window = limits.value(id).toObject();
        const QJsonValue percentValue = window.value(QStringLiteral("usedPercent"));
        if (!percentValue.isDouble())
            continue;
        const double rawPercent = percentValue.toDouble();
        if (!qIsFinite(rawPercent) || rawPercent < 0.0 || rawPercent > 100.0)
            continue;

        const double minutes = window.value(QStringLiteral("windowDurationMins")).toDouble(-1);
        const double resetSeconds = window.value(QStringLiteral("resetsAt")).toDouble(-1);
        QVariantMap item{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), windowLabel(minutes, id)},
            {QStringLiteral("percent"), qRound(rawPercent)},
        };
        if (qIsFinite(resetSeconds) && resetSeconds >= 0 && resetSeconds <= 4'102'444'800.0)
            item.insert(QStringLiteral("resetsAt"),
                        QDateTime::fromSecsSinceEpoch(qRound64(resetSeconds)).toLocalTime());
        result.append(item);
    }
    return result;
}

QString UsageBackend::windowLabel(double minutes, const QString &fallback)
{
    if (!qIsFinite(minutes) || minutes <= 0 || minutes > 10 * 365 * 24 * 60)
        return fallback == QStringLiteral("primary") ? QStringLiteral("Current session")
                                                     : QStringLiteral("Longer window");
    if (minutes < 60)
        return QString::number(qRound(minutes)) + QStringLiteral("m limit");
    if (minutes < 24 * 60)
        return QString::number(qRound(minutes / 60)) + QStringLiteral("h limit");
    const int days = qRound(minutes / (24 * 60));
    if (days == 7)
        return QStringLiteral("Weekly limit");
    if (days == 30)
        return QStringLiteral("Monthly limit");
    return QString::number(days) + QStringLiteral("d limit");
}

QString UsageBackend::findCodex()
{
    return findCodex(QDir::homePath(), {});
}

QString UsageBackend::findCodex(const QString &home, const QStringList &executablePaths)
{
    const auto trusted = [](const QString &candidate) {
        const QString canonical = QFileInfo(candidate).canonicalFilePath();
        return isTrustedExecutable(canonical) ? canonical : QString{};
    };

    QString executable = QStandardPaths::findExecutable(QStringLiteral("codex"), executablePaths);
    if (!(executable = trusted(executable)).isEmpty())
        return executable;

    executable = trusted(QDir(home).filePath(QStringLiteral(".local/bin/codex")));
    if (!executable.isEmpty())
        return executable;

    const QStringList extensionRoots{
        QStringLiteral(".vscode/extensions"),
        QStringLiteral(".vscode-insiders/extensions"),
        QStringLiteral(".cursor/extensions"),
    };
    for (const QString &root : extensionRoots) {
        QDir extensions(QDir(home).filePath(root));
        const QStringList versions = extensions.entryList(
            {QStringLiteral("openai.chatgpt-*-linux-*")},
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name | QDir::Reversed);
        for (const QString &version : versions) {
            const QDir bin(extensions.filePath(version + QStringLiteral("/bin")));
            const QStringList platforms = bin.entryList(
                {QStringLiteral("linux-x86_64"), QStringLiteral("linux-aarch64")},
                QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &platform : platforms) {
                executable = trusted(bin.filePath(platform + QStringLiteral("/codex")));
                if (!executable.isEmpty())
                    return executable;
            }
        }
    }
    return {};
}

bool UsageBackend::isTrustedExecutable(const QString &path)
{
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || !info.isFile() || !info.isExecutable()
        || info.isSymLink())
        return false;
    const uint owner = info.ownerId();
    if (owner != 0 && owner != geteuid())
        return false;
    const QFile::Permissions unsafe = QFile::WriteGroup | QFile::WriteOther;
    return !(info.permissions() & unsafe);
}

void UsageBackend::finishWithError(const QString &message)
{
    m_timeout.stop();
    setStatus(message);
}

void UsageBackend::stopProcess()
{
    if (m_process.state() == QProcess::NotRunning)
        return;
    m_process.terminate();
    if (!m_process.waitForFinished(250)) {
        m_process.kill();
        m_process.waitForFinished(250);
    }
}

void UsageBackend::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    Q_EMIT statusChanged();
}
