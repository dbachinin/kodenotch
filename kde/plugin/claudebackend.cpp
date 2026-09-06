#include "claudebackend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QtMath>

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
void wipe(QByteArray &value)
{
    value.fill('\0');
    value.clear();
}
}

ClaudeBackend::ClaudeBackend(QObject *parent)
    : QObject(parent)
    , m_network(this)
{
    m_refreshTimer.setInterval(5 * 60 * 1000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ClaudeBackend::refresh);
}

int ClaudeBackend::percent() const
{
    if (m_windows.isEmpty())
        return 0;
    return m_windows.first().toMap().value(QStringLiteral("percent")).toInt();
}

void ClaudeBackend::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    Q_EMIT enabledChanged();

    if (!enabled) {
        m_refreshTimer.stop();
        clearReply();
        wipe(m_body);
        m_windows.clear();
        Q_EMIT readingChanged();
        setStatus(QStringLiteral("Disabled"));
        return;
    }

    m_refreshTimer.start();
    refresh();
}

void ClaudeBackend::refresh()
{
    if (!m_enabled || m_reply)
        return;

    QString error;
    QByteArray token = loadAccessToken(credentialPath(), &error);
    if (token.isEmpty()) {
        setStatus(error);
        return;
    }

    QNetworkRequest request(QUrl(QStringLiteral("https://api.anthropic.com/api/oauth/usage")));
    QByteArray authorization = QByteArrayLiteral("Bearer ") + token;
    request.setRawHeader(QByteArrayLiteral("Authorization"), authorization);
    request.setRawHeader(QByteArrayLiteral("anthropic-beta"), QByteArrayLiteral("oauth-2025-04-20"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15'000);
    token.fill('\0');
    authorization.fill('\0');

    wipe(m_body);
    setStatus(QStringLiteral("Refreshing…"));
    m_reply = m_network.get(request);
    m_reply->setReadBufferSize(MaxReplyBytes + 1);
    connect(m_reply, &QNetworkReply::readyRead, this, &ClaudeBackend::consumeReply);
    connect(m_reply, &QNetworkReply::finished, this, &ClaudeBackend::finishReply);
    Q_EMIT busyChanged();
}

void ClaudeBackend::consumeReply()
{
    if (!m_reply)
        return;
    m_body += m_reply->readAll();
    if (m_body.size() > MaxReplyBytes)
        rejectReply(QStringLiteral("Claude reply was too large"));
}

void ClaudeBackend::finishReply()
{
    if (!m_reply)
        return;
    m_body += m_reply->readAll();
    if (m_body.size() > MaxReplyBytes) {
        rejectReply(QStringLiteral("Claude reply was too large"));
        return;
    }

    QNetworkReply *reply = m_reply;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    m_reply = nullptr;
    reply->deleteLater();
    Q_EMIT busyChanged();

    if (!m_enabled) {
        wipe(m_body);
        return;
    }
    if (httpStatus == 401 || httpStatus == 403) {
        wipe(m_body);
        setStatus(QStringLiteral("Sign in to Claude Code"));
        return;
    }
    if (httpStatus == 429) {
        wipe(m_body);
        setStatus(QStringLiteral("Rate limited; retrying later"));
        return;
    }
    if (httpStatus >= 300 && httpStatus < 400) {
        wipe(m_body);
        setStatus(QStringLiteral("Claude redirect refused"));
        return;
    }
    if (networkError != QNetworkReply::NoError || httpStatus < 200 || httpStatus >= 300) {
        wipe(m_body);
        setStatus(httpStatus > 0
                      ? QStringLiteral("Claude HTTP %1").arg(httpStatus)
                      : QStringLiteral("Could not reach Claude"));
        return;
    }

    const QVariantList parsed = parseUsage(m_body);
    wipe(m_body);
    if (parsed.isEmpty()) {
        setStatus(QStringLiteral("Claude returned no valid limits"));
        return;
    }
    m_windows = parsed;
    Q_EMIT readingChanged();
    setStatus(QStringLiteral("Up to date"));
}

QByteArray ClaudeBackend::loadAccessToken(const QString &path, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QByteArray{};
    };

    const QByteArray encodedPath = QFile::encodeName(path);
    const int descriptor = ::open(encodedPath.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0 && errno == ENOENT)
        return fail(QStringLiteral("Sign in to Claude Code"));
    if (descriptor < 0)
        return fail(QStringLiteral("Could not read Claude credentials"));

    struct stat metadata {};
    if (::fstat(descriptor, &metadata) != 0) {
        ::close(descriptor);
        return fail(QStringLiteral("Could not read Claude credentials"));
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_uid != geteuid()
        || (metadata.st_mode & 0077) != 0) {
        ::close(descriptor);
        return fail(QStringLiteral("Claude credential file is not private"));
    }
    if (metadata.st_size <= 0 || metadata.st_size > MaxCredentialBytes) {
        ::close(descriptor);
        return fail(QStringLiteral("Claude credential file has an invalid size"));
    }

    QFile file;
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        return fail(QStringLiteral("Could not read Claude credentials"));
    }
    const QJsonObject oauth = QJsonDocument::fromJson(file.readAll()).object()
                                  .value(QStringLiteral("claudeAiOauth")).toObject();
    const QString token = oauth.value(QStringLiteral("accessToken")).toString();
    const double expiresAt = oauth.value(QStringLiteral("expiresAt")).toDouble(-1);
    if (token.isEmpty() || token.size() > 64 * 1024)
        return fail(QStringLiteral("Claude credential is invalid"));
    if (!qIsFinite(expiresAt) || expiresAt <= QDateTime::currentMSecsSinceEpoch()
        || expiresAt > 4'102'444'800'000.0)
        return fail(QStringLiteral("Claude credential has expired"));
    return token.toUtf8();
}

QVariantList ClaudeBackend::parseUsage(const QByteArray &data)
{
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    QList<QVariantMap> windows;

    const auto add = [&windows](const QString &kind, double percent, const QString &reset) {
        static const QRegularExpression safeKind(QStringLiteral("^[a-z0-9_]{1,64}$"));
        const QDateTime resetAt = parseDate(reset);
        if (!safeKind.match(kind).hasMatch() || !qIsFinite(percent) || percent < 0
            || percent > 100 || !resetAt.isValid())
            return;
        windows.append({
            {QStringLiteral("id"), kind},
            {QStringLiteral("label"), labelForKind(kind)},
            {QStringLiteral("percent"), qRound(percent)},
            {QStringLiteral("resetsAt"), resetAt.toLocalTime()},
        });
    };

    for (const QJsonValue &value : root.value(QStringLiteral("limits")).toArray()) {
        const QJsonObject limit = value.toObject();
        add(limit.value(QStringLiteral("kind")).toString(),
            limit.value(QStringLiteral("percent")).toDouble(-1),
            limit.value(QStringLiteral("resets_at")).toString());
    }

    const auto mergeLegacy = [&root, &windows, &add](const QString &key, const QString &id) {
        if (std::any_of(windows.cbegin(), windows.cend(), [&id](const QVariantMap &item) {
                return item.value(QStringLiteral("id")).toString() == id;
            }))
            return;
        const QJsonObject window = root.value(key).toObject();
        add(id, window.value(QStringLiteral("utilization")).toDouble(-1),
            window.value(QStringLiteral("resets_at")).toString());
    };
    mergeLegacy(QStringLiteral("five_hour"), QStringLiteral("session"));
    mergeLegacy(QStringLiteral("seven_day"), QStringLiteral("weekly_all"));

    const auto rank = [](const QVariantMap &item) {
        const QString id = item.value(QStringLiteral("id")).toString();
        return id == QStringLiteral("session") ? 0 : id == QStringLiteral("weekly_all") ? 1 : 2;
    };
    std::sort(windows.begin(), windows.end(), [&rank](const QVariantMap &a, const QVariantMap &b) {
        const int aRank = rank(a);
        const int bRank = rank(b);
        return aRank == bRank
            ? a.value(QStringLiteral("id")).toString() < b.value(QStringLiteral("id")).toString()
            : aRank < bRank;
    });

    QVariantList result;
    for (const QVariantMap &window : std::as_const(windows))
        result.append(window);
    return result;
}

QString ClaudeBackend::credentialPath()
{
    return QDir::home().filePath(QStringLiteral(".claude/.credentials.json"));
}

QString ClaudeBackend::labelForKind(const QString &kind)
{
    if (kind == QStringLiteral("session"))
        return QStringLiteral("Current session");
    if (kind == QStringLiteral("weekly_all"))
        return QStringLiteral("All models");
    if (kind == QStringLiteral("weekly_opus"))
        return QStringLiteral("Opus");
    if (kind == QStringLiteral("weekly_sonnet"))
        return QStringLiteral("Sonnet");
    QString label = kind;
    label.remove(QStringLiteral("weekly_"));
    label.replace(u'_', u' ');
    if (!label.isEmpty())
        label[0] = label[0].toUpper();
    return label;
}

QDateTime ClaudeBackend::parseDate(const QString &value)
{
    QDateTime date = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!date.isValid())
        date = QDateTime::fromString(value, Qt::ISODate);
    const qint64 seconds = date.toSecsSinceEpoch();
    return date.isValid() && seconds >= 0 && seconds <= 4'102'444'800 ? date : QDateTime{};
}

void ClaudeBackend::rejectReply(const QString &status)
{
    clearReply();
    wipe(m_body);
    setStatus(status);
}

void ClaudeBackend::clearReply()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    Q_EMIT busyChanged();
}

void ClaudeBackend::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    Q_EMIT statusChanged();
}
