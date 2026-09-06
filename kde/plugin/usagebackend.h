#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

class UsageBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool hasReading READ hasReading NOTIFY readingChanged)
    Q_PROPERTY(int percent READ percent NOTIFY readingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY readingChanged)

public:
    explicit UsageBackend(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    bool busy() const { return m_process.state() != QProcess::NotRunning; }
    bool hasReading() const { return !m_windows.isEmpty(); }
    int percent() const;
    QString status() const { return m_status; }
    QVariantList windows() const { return m_windows; }

    void setEnabled(bool enabled);
    Q_INVOKABLE void refresh();

    // Public so malformed provider replies can be tested without starting Codex.
    static QVariantList parseRateLimits(const QByteArray &line);
    static bool isTrustedExecutable(const QString &path);
    static QString findCodex(const QString &home, const QStringList &executablePaths);

Q_SIGNALS:
    void enabledChanged();
    void busyChanged();
    void readingChanged();
    void statusChanged();

private:
    static QString findCodex();
    static QString windowLabel(double minutes, const QString &fallback);
    void finishWithError(const QString &message);
    void consumeOutput();
    void stopProcess();
    void setStatus(const QString &status);

    bool m_enabled = false;
    quint64 m_generation = 0;
    QProcess m_process;
    QTimer m_refreshTimer;
    QTimer m_timeout;
    QByteArray m_output;
    QVariantList m_windows;
    QString m_status = QStringLiteral("Disabled");
    bool m_receivedReply = false;

    static constexpr qsizetype MaxReplyBytes = 1024 * 1024;
};
