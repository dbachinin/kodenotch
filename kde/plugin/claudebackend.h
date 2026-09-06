#pragma once

#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

class QNetworkReply;

class ClaudeBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool hasReading READ hasReading NOTIFY readingChanged)
    Q_PROPERTY(int percent READ percent NOTIFY readingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY readingChanged)

public:
    explicit ClaudeBackend(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    bool busy() const { return !m_reply.isNull(); }
    bool hasReading() const { return !m_windows.isEmpty(); }
    int percent() const;
    QString status() const { return m_status; }
    QVariantList windows() const { return m_windows; }

    void setEnabled(bool enabled);
    Q_INVOKABLE void refresh();

    static QByteArray loadAccessToken(const QString &path, QString *error = nullptr);
    static QVariantList parseUsage(const QByteArray &data);

Q_SIGNALS:
    void enabledChanged();
    void busyChanged();
    void readingChanged();
    void statusChanged();

private:
    static QString credentialPath();
    static QString labelForKind(const QString &kind);
    static QDateTime parseDate(const QString &value);
    void consumeReply();
    void finishReply();
    void rejectReply(const QString &status);
    void clearReply();
    void setStatus(const QString &status);

    bool m_enabled = false;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_refreshTimer;
    QByteArray m_body;
    QVariantList m_windows;
    QString m_status = QStringLiteral("Disabled");

    static constexpr qsizetype MaxCredentialBytes = 128 * 1024;
    static constexpr qsizetype MaxReplyBytes = 1024 * 1024;
};
