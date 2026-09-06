#pragma once

#include <QDateTime>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <optional>

class QNetworkReply;

class AntigravityBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool hasReading READ hasReading NOTIFY readingChanged)
    Q_PROPERTY(int percent READ percent NOTIFY readingChanged)
    // Requests seen in local transcripts today, when the language server is
    // not answering and a count is all there is. 0 while a real reading exists.
    Q_PROPERTY(int requestsToday READ requestsToday NOTIFY readingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY readingChanged)

public:
    struct Endpoint {
        int pid = 0;
        QString csrfToken;
        QList<int> ports;

        bool operator==(const Endpoint &other) const = default;
    };

    explicit AntigravityBackend(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    bool hasReading() const { return !m_windows.isEmpty(); }
    int percent() const;
    int requestsToday() const { return m_requestsToday; }
    QString status() const { return m_status; }
    QVariantList windows() const { return m_windows; }

    void setEnabled(bool enabled);
    Q_INVOKABLE void refresh();

    // Public for testability
    static QList<int> parsePorts(const QString &lsofOutput);
    static QVariantList parseQuotaSummary(const QByteArray &data);
    static int countRequestsToday(const QString &brainDir, const QDateTime &now = QDateTime::currentDateTime());
    static std::optional<Endpoint> discoverEndpoint(
        const std::function<QList<int>(int)> &portResolver = nullptr);
    static QList<int> listeningPortsOfPid(int pid);

Q_SIGNALS:
    void enabledChanged();
    void busyChanged();
    void readingChanged();
    void statusChanged();

private:
    static QString brainPath();
    static QDateTime parseDate(const QString &value);

    void probeNextPort();
    void consumeReply();
    void finishReply();
    void finishWithWindows(const QVariantList &windows);
    void fallbackToActivity();
    void clearReply();
    void setBusy(bool busy);
    void setStatus(const QString &status);

    bool m_enabled = false;
    bool m_busy = false;
    bool m_everBridged = false;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_refreshTimer;
    QByteArray m_body;
    QVariantList m_windows;
    int m_requestsToday = 0;
    QString m_status = QStringLiteral("Disabled");

    QString m_currentCsrfToken;
    QList<int> m_candidatePorts;
    int m_currentPortIndex = 0;

    static constexpr qsizetype MaxReplyBytes = 1024 * 1024;
};
