#include "antigravitybackend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QtMath>

namespace {
void wipe(QByteArray &value)
{
    value.fill('\0');
    value.clear();
}
}

AntigravityBackend::AntigravityBackend(QObject *parent)
    : QObject(parent)
    , m_network(this)
{
    m_refreshTimer.setInterval(5 * 60 * 1000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &AntigravityBackend::refresh);
}

int AntigravityBackend::percent() const
{
    if (m_windows.isEmpty())
        return 0;

    for (const QVariant &item : m_windows) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("id")).toString() == QStringLiteral("gemini-weekly"))
            return map.value(QStringLiteral("percent")).toInt();
    }
    return m_windows.first().toMap().value(QStringLiteral("percent")).toInt();
}

void AntigravityBackend::setEnabled(bool enabled)
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
        m_requestsToday = 0;
        m_everBridged = false;
        m_candidatePorts.clear();
        m_currentPortIndex = 0;
        setBusy(false);
        Q_EMIT readingChanged();
        setStatus(QStringLiteral("Disabled"));
        return;
    }

    m_refreshTimer.start();
    refresh();
}

void AntigravityBackend::refresh()
{
    if (!m_enabled || m_busy)
        return;

    clearReply();
    wipe(m_body);
    setBusy(true);
    setStatus(QStringLiteral("Refreshing…"));

    auto endpoint = discoverEndpoint();
    if (!endpoint || endpoint->ports.isEmpty()) {
        fallbackToActivity();
        return;
    }

    m_currentCsrfToken = endpoint->csrfToken;
    m_candidatePorts = endpoint->ports;
    m_currentPortIndex = 0;
    if (m_currentCsrfToken.isEmpty())
        fetchHubToken(endpoint->hubPort);
    else
        probeNextPort();
}

void AntigravityBackend::fetchHubToken(int port)
{
    clearReply();
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(port)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(10'000);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, &AntigravityBackend::finishHubToken);
}

void AntigravityBackend::finishHubToken()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    if (!m_enabled) {
        setBusy(false);
        return;
    }

    m_currentCsrfToken = hubCsrfToken(reply->read(MaxReplyBytes));
    if (m_currentCsrfToken.isEmpty())
        fallbackToActivity();
    else
        probeNextPort();
}

QString AntigravityBackend::hubCsrfToken(const QByteArray &page)
{
    // The page inlines `window.__APP_CONFIG__ = {"csrfToken":"…", …}`.
    static const QRegularExpression pattern(QStringLiteral(R"re("csrfToken"\s*:\s*"([^"]+)")re"));
    return pattern.match(QString::fromUtf8(page)).captured(1);
}

void AntigravityBackend::probeNextPort()
{
    if (!m_enabled) {
        setBusy(false);
        return;
    }

    if (m_currentPortIndex >= m_candidatePorts.size()) {
        fallbackToActivity();
        return;
    }

    const int port = m_candidatePorts.at(m_currentPortIndex);
    clearReply();
    wipe(m_body);

    const QUrl url(QStringLiteral("https://127.0.0.1:%1/exa.language_server_pb.LanguageServerService/RetrieveUserQuotaSummary").arg(port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("x-codeium-csrf-token"), m_currentCsrfToken.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(10'000);

    QSslConfiguration ssl = request.sslConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(ssl);

    m_reply = m_network.post(request, QByteArrayLiteral("{\"forceRefresh\":true}"));
    m_reply->setReadBufferSize(MaxReplyBytes + 1);

    connect(m_reply, &QNetworkReply::sslErrors, m_reply, [this]() {
        if (m_reply)
            m_reply->ignoreSslErrors();
    });
    connect(m_reply, &QNetworkReply::readyRead, this, &AntigravityBackend::consumeReply);
    connect(m_reply, &QNetworkReply::finished, this, &AntigravityBackend::finishReply);
}

void AntigravityBackend::consumeReply()
{
    if (!m_reply)
        return;

    m_body += m_reply->readAll();
    if (m_body.size() > MaxReplyBytes) {
        clearReply();
        wipe(m_body);
        m_currentPortIndex++;
        probeNextPort();
    }
}

void AntigravityBackend::finishReply()
{
    if (!m_reply)
        return;

    m_body += m_reply->readAll();
    QNetworkReply *reply = m_reply;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    m_reply = nullptr;
    reply->deleteLater();

    if (!m_enabled) {
        wipe(m_body);
        setBusy(false);
        return;
    }

    if (networkError == QNetworkReply::NoError && httpStatus == 200 && m_body.size() <= MaxReplyBytes) {
        const QVariantList parsed = parseQuotaSummary(m_body);
        wipe(m_body);
        if (!parsed.isEmpty()) {
            finishWithWindows(parsed);
            return;
        }
    }

    wipe(m_body);
    m_currentPortIndex++;
    probeNextPort();
}

void AntigravityBackend::finishWithWindows(const QVariantList &windows)
{
    m_windows = windows;
    m_requestsToday = 0;
    m_everBridged = true;
    setBusy(false);
    Q_EMIT readingChanged();
    setStatus(QStringLiteral("Up to date"));
}

void AntigravityBackend::fallbackToActivity()
{
    setBusy(false);
    if (m_everBridged) {
        setStatus(QStringLiteral("Antigravity is not running"));
        return;
    }

    m_windows.clear();
    m_requestsToday = countRequestsToday(brainPath());
    Q_EMIT readingChanged();

    if (m_requestsToday > 0) {
        setStatus(QStringLiteral("~%1 request%2 today")
                      .arg(m_requestsToday)
                      .arg(m_requestsToday == 1 ? QString() : QStringLiteral("s")));
    } else {
        setStatus(QStringLiteral("Antigravity is not running"));
    }
}

void AntigravityBackend::clearReply()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void AntigravityBackend::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void AntigravityBackend::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    Q_EMIT statusChanged();
}

QString AntigravityBackend::brainPath()
{
    return QDir::home().filePath(QStringLiteral(".gemini/antigravity/brain"));
}

QDateTime AntigravityBackend::parseDate(const QString &value)
{
    QDateTime date = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!date.isValid())
        date = QDateTime::fromString(value, Qt::ISODate);
    const qint64 seconds = date.toSecsSinceEpoch();
    return date.isValid() && seconds >= 0 && seconds <= 4'102'444'800 ? date : QDateTime{};
}

QList<int> AntigravityBackend::parsePorts(const QString &lsofOutput)
{
    QList<int> ports;
    const QStringList lines = lsofOutput.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (it->contains(QLatin1Char(':'))) {
                const QString portStr = it->section(QLatin1Char(':'), -1);
                bool ok = false;
                int port = portStr.toInt(&ok);
                if (ok && port > 0 && port <= 65535 && !ports.contains(port)) {
                    ports.append(port);
                }
                break;
            }
        }
    }
    return ports;
}

QList<int> AntigravityBackend::listeningPortsOfPid(int pid)
{
    if (pid <= 0)
        return {};

    QProcess lsof;
    lsof.start(QStringLiteral("lsof"), {
        QStringLiteral("-nP"),
        QStringLiteral("-a"),
        QStringLiteral("-p"),
        QString::number(pid),
        QStringLiteral("-iTCP"),
        QStringLiteral("-sTCP:LISTEN")
    });
    if (!lsof.waitForFinished(1000))
        return {};

    return parsePorts(QString::fromUtf8(lsof.readAllStandardOutput()));
}

std::optional<AntigravityBackend::Endpoint> AntigravityBackend::parseCommandLine(const QByteArrayList &args)
{
    if (args.isEmpty())
        return std::nullopt;

    Endpoint ep;
    bool isServer = false;
    for (int i = 0; i < args.size(); ++i) {
        const QString arg = QString::fromUtf8(args[i]);
        if (i == 0)
            isServer = arg.contains(QStringLiteral("language_server"))
                || (QFileInfo(arg).fileName() == QStringLiteral("agy") && args.contains(QByteArrayLiteral("--hub")));
        else if (arg == QStringLiteral("--csrf_token") && i + 1 < args.size())
            ep.csrfToken = QString::fromUtf8(args[i + 1]);
        else if (arg.startsWith(QStringLiteral("--hub-port=")))
            ep.hubPort = arg.mid(11).toInt();
        else if (arg == QStringLiteral("--hub-port") && i + 1 < args.size())
            ep.hubPort = args[i + 1].toInt();
    }
    if (!isServer || (ep.csrfToken.isEmpty() && ep.hubPort <= 0))
        return std::nullopt;
    return ep;
}

std::optional<AntigravityBackend::Endpoint> AntigravityBackend::discoverEndpoint(
    const std::function<QList<int>(int)> &portResolver)
{
    const QDir procDir(QStringLiteral("/proc"));
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        const int pid = entry.toInt(&ok);
        if (!ok || pid <= 0)
            continue;

        QFile cmdlineFile(procDir.filePath(entry + QStringLiteral("/cmdline")));
        if (!cmdlineFile.open(QIODevice::ReadOnly))
            continue;
        auto ep = parseCommandLine(cmdlineFile.readAll().split('\0'));
        if (!ep)
            continue;

        ep->pid = pid;
        ep->ports = portResolver ? portResolver(pid) : listeningPortsOfPid(pid);
        if (!ep->ports.isEmpty())
            return ep;
    }
    return std::nullopt;
}

QVariantList AntigravityBackend::parseQuotaSummary(const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return {};

    const QJsonObject root = doc.object();
    QJsonArray groups = root.value(QStringLiteral("response")).toObject().value(QStringLiteral("groups")).toArray();
    if (groups.isEmpty())
        groups = root.value(QStringLiteral("groups")).toArray();

    QVariantList windows;
    for (const QJsonValue &groupVal : std::as_const(groups)) {
        const QJsonObject groupObj = groupVal.toObject();
        const QString groupName = groupObj.value(QStringLiteral("displayName")).toString();
        const QJsonArray buckets = groupObj.value(QStringLiteral("buckets")).toArray();

        for (const QJsonValue &bucketVal : buckets) {
            const QJsonObject bucketObj = bucketVal.toObject();
            const QJsonValue remVal = bucketObj.value(QStringLiteral("remainingFraction"));
            if (!remVal.isDouble())
                continue;

            const double remaining = remVal.toDouble();
            if (!qIsFinite(remaining) || remaining < 0.0 || remaining > 1.0)
                continue;

            const QString bucketId = bucketObj.value(QStringLiteral("bucketId")).toString();
            const QString bucketName = bucketObj.value(QStringLiteral("displayName")).toString();
            QVariantMap item{
                {QStringLiteral("id"), !bucketId.isEmpty() ? bucketId : (!groupName.isEmpty() ? groupName : QStringLiteral("quota"))},
                {QStringLiteral("label"), !groupName.isEmpty() ? groupName : (!bucketName.isEmpty() ? bucketName : QStringLiteral("Usage"))},
                {QStringLiteral("percent"), qRound((1.0 - remaining) * 100.0)},
            };

            const QDateTime resetsAt = parseDate(bucketObj.value(QStringLiteral("resetTime")).toString());
            if (resetsAt.isValid())
                item.insert(QStringLiteral("resetsAt"), resetsAt.toLocalTime());
            windows.append(item);
        }
    }
    return windows;
}

int AntigravityBackend::countRequestsToday(const QString &brainDir, const QDateTime &now)
{
    QDir dir(brainDir);
    if (!dir.exists())
        return 0;

    const QStringList trajectories = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    int count = 0;
    const QDate today = now.toLocalTime().date();

    for (const QString &traj : trajectories) {
        const QString transcriptPath = dir.filePath(traj + QStringLiteral("/.system_generated/logs/transcript.jsonl"));
        // Entries are appended as they happen, so a transcript last written
        // before today cannot hold today's requests. Skips the JSON parse for
        // every old session.
        if (QFileInfo(transcriptPath).lastModified().toLocalTime().date() < today)
            continue;
        QFile file(transcriptPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        while (!file.atEnd()) {
            const QByteArray line = file.readLine();
            if (line.isEmpty() || line.size() > 512 * 1024)
                continue;

            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject())
                continue;

            const QJsonObject obj = doc.object();
            if (obj.value(QStringLiteral("source")).toString() != QStringLiteral("MODEL"))
                continue;

            const QDateTime dt = parseDate(obj.value(QStringLiteral("created_at")).toString());
            if (dt.isValid() && dt.toLocalTime().date() == today)
                count++;
        }
    }
    return count;
}
