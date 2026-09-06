#include "usagebackend.h"
#include "antigravitybackend.h"
#include "claudebackend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class UsageBackendTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsDisabled()
    {
        UsageBackend backend;
        QVERIFY(!backend.enabled());
        QVERIFY(!backend.busy());
        QVERIFY(!backend.hasReading());
        QCOMPARE(backend.status(), QStringLiteral("Disabled"));
        backend.refresh();
        QVERIFY(!backend.busy());
    }

    void parsesValidReply()
    {
        const QByteArray reply = R"({"jsonrpc":"2.0","id":2,"result":{"rateLimits":{"primary":{"usedPercent":42.4,"windowDurationMins":300,"resetsAt":1800000000},"secondary":{"usedPercent":7,"windowDurationMins":10080}}}})";
        const QVariantList windows = UsageBackend::parseRateLimits(reply);
        QCOMPARE(windows.size(), 2);
        QCOMPARE(windows.first().toMap().value(QStringLiteral("percent")).toInt(), 42);
        QCOMPARE(windows.first().toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("5h limit"));
    }

    void rejectsMalformedAndOutOfRangeReplies()
    {
        QCOMPARE(UsageBackend::parseRateLimits(QByteArrayLiteral("not json")).size(), 0);
        const QByteArray reply = R"({"id":2,"result":{"rateLimits":{"primary":{"usedPercent":1000,"resetsAt":1e300}}}})";
        QCOMPARE(UsageBackend::parseRateLimits(reply).size(), 0);
        const QByteArray hugeReset = R"({"id":2,"result":{"rateLimits":{"primary":{"usedPercent":50,"resetsAt":1e300}}}})";
        const QVariantList windows = UsageBackend::parseRateLimits(hugeReset);
        QCOMPARE(windows.size(), 1);
        QVERIFY(!windows.first().toMap().contains(QStringLiteral("resetsAt")));
    }

    void rejectsWritableExecutable()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("codex"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n");
        file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                    | QFile::WriteOther));
        QVERIFY(!UsageBackend::isTrustedExecutable(path));
    }

    void readsAndForgetsAnEndToEndReply()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("codex"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"(#!/bin/sh
while IFS= read -r line; do
    case "$line" in
        *account/rateLimits/read*)
            printf '%s\n' '{"id":2,"result":{"rateLimits":{"primary":{"usedPercent":63,"windowDurationMins":300}}}}'
            ;;
    esac
done
)");
        file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", directory.path().toUtf8());

        UsageBackend backend;
        backend.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(backend.hasReading(), 3000);
        QCOMPARE(backend.percent(), 63);
        QCOMPARE(backend.status(), QStringLiteral("Up to date"));

        backend.setEnabled(false);
        QVERIFY(!backend.hasReading());
        QVERIFY(!backend.busy());
        QCOMPARE(backend.status(), QStringLiteral("Disabled"));
        qputenv("PATH", oldPath);
    }

    void findsTrustedCodexBundledWithTheOpenAIExtension()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString directory = home.filePath(
            QStringLiteral(".vscode/extensions/openai.chatgpt-1.2.3-linux-x64/bin/linux-x86_64"));
        QVERIFY(QDir().mkpath(directory));
        const QString path = QDir(directory).filePath(QStringLiteral("codex"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n");
        file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        QCOMPARE(UsageBackend::findCodex(home.path(), {home.filePath(QStringLiteral("bin"))}),
                 QFileInfo(path).canonicalFilePath());
    }

    void parsesClaudeUsage()
    {
        const QByteArray reply = R"({"limits":[{"kind":"weekly_all","percent":17,"resets_at":"2026-09-12T17:00:00.000Z"},{"kind":"session","percent":52,"resets_at":"2026-09-06T21:00:00.000Z"}]})";
        const QVariantList windows = ClaudeBackend::parseUsage(reply);
        QCOMPARE(windows.size(), 2);
        QCOMPARE(windows.first().toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("session"));
        QCOMPARE(windows.first().toMap().value(QStringLiteral("percent")).toInt(), 52);
    }

    void readsOnlyPrivateClaudeCredentialFiles()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("credentials.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray json = QByteArrayLiteral("{\"claudeAiOauth\":{\"accessToken\":\"test-token\",\"expiresAt\":")
            + QByteArray::number(QDateTime::currentMSecsSinceEpoch() + 60'000)
            + QByteArrayLiteral("}}");
        file.write(json);
        file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));

        QString error;
        QCOMPARE(ClaudeBackend::loadAccessToken(path, &error), QByteArrayLiteral("test-token"));
        QVERIFY(error.isEmpty());

        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadOther));
        QVERIFY(ClaudeBackend::loadAccessToken(path, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("Claude credential file is not private"));

        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
        const QString linkPath = directory.filePath(QStringLiteral("credentials-link.json"));
        QVERIFY(QFile::link(path, linkPath));
        QVERIFY(ClaudeBackend::loadAccessToken(linkPath, &error).isEmpty());
    }

    void antigravityStartsDisabled()
    {
        AntigravityBackend backend;
        QVERIFY(!backend.enabled());
        QVERIFY(!backend.busy());
        QVERIFY(!backend.hasReading());
        QCOMPARE(backend.percent(), 0);
        QCOMPARE(backend.status(), QStringLiteral("Disabled"));
        backend.refresh();
        QVERIFY(!backend.busy());
    }

    void parsesAntigravityQuotaReply()
    {
        const QByteArray reply = R"({"response":{"groups":[
          {"displayName":"Gemini Models",
           "description":"Models within this group: Gemini Flash, Gemini Pro",
           "buckets":[{"bucketId":"gemini-weekly","displayName":"Weekly Limit Remaining",
                       "window":"weekly","remainingFraction":0.96262,
                       "resetTime":"2026-09-07T14:12:34Z"}]},
          {"displayName":"Claude and GPT models",
           "buckets":[{"bucketId":"3p-weekly","displayName":"Weekly Limit Remaining",
                       "window":"weekly","remainingFraction":1.0,
                       "resetTime":"2026-09-08T09:12:10Z"}]}]}})";
        const QVariantList windows = AntigravityBackend::parseQuotaSummary(reply);
        QCOMPARE(windows.size(), 2);
        QCOMPARE(windows[0].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("gemini-weekly"));
        QCOMPARE(windows[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Gemini Models"));
        QCOMPARE(windows[0].toMap().value(QStringLiteral("percent")).toInt(), 4);
        QVERIFY(windows[0].toMap().contains(QStringLiteral("resetsAt")));

        QCOMPARE(windows[1].toMap().value(QStringLiteral("id")).toString(), QStringLiteral("3p-weekly"));
        QCOMPARE(windows[1].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Claude and GPT models"));
        QCOMPARE(windows[1].toMap().value(QStringLiteral("percent")).toInt(), 0);
    }

    void rejectsMalformedAntigravityReplies()
    {
        QCOMPARE(AntigravityBackend::parseQuotaSummary(QByteArrayLiteral("not json")).size(), 0);

        const QByteArray wildFractions =
            R"({"response":{"groups":[{"displayName":"G","buckets":[{"bucketId":"a","remainingFraction":1.4},{"bucketId":"b","remainingFraction":-0.2}]}]}})";
        QCOMPARE(AntigravityBackend::parseQuotaSummary(wildFractions).size(), 0);
    }

    void parsesAntigravityLsofPorts()
    {
        const QString output = QStringLiteral(
            "language_server 29283 vinz 12u IPv4 0x1 0t0 TCP 127.0.0.1:63881 (LISTEN)\n"
            "language_server 29283 vinz 13u IPv4 0x2 0t0 TCP 127.0.0.1:63882 (LISTEN)\n");
        const QList<int> ports = AntigravityBackend::parsePorts(output);
        QCOMPARE(ports.size(), 2);
        QCOMPARE(ports, QList<int>({63881, 63882}));
    }

    void countsAntigravityActivityToday()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString traj1 = dir.filePath(QStringLiteral("t1/.system_generated/logs"));
        const QString traj2 = dir.filePath(QStringLiteral("t2/.system_generated/logs"));
        QVERIFY(QDir().mkpath(traj1));
        QVERIFY(QDir().mkpath(traj2));

        const QDateTime now = QDateTime::currentDateTime();
        const QString todayUtc = now.toUTC().toString(Qt::ISODate);
        const QString yesterdayUtc = now.addDays(-1).toUTC().toString(Qt::ISODate);

        QFile f1(traj1 + QStringLiteral("/transcript.jsonl"));
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.write(QString(
            "{\"source\":\"USER_EXPLICIT\",\"created_at\":\"%1\"}\n"
            "{\"source\":\"MODEL\",\"created_at\":\"%1\"}\n"
            "{\"source\":\"MODEL\",\"created_at\":\"%2\"}\n"
        ).arg(todayUtc, yesterdayUtc).toUtf8());
        f1.close();

        QFile f2(traj2 + QStringLiteral("/transcript.jsonl"));
        QVERIFY(f2.open(QIODevice::WriteOnly));
        f2.write(QString(
            "not json\n"
            "{\"source\":\"MODEL\",\"created_at\":\"%1\"}\n"
        ).arg(todayUtc).toUtf8());
        f2.close();

        int count = AntigravityBackend::countRequestsToday(dir.path(), now);
        QCOMPARE(count, 2);
    }

    void antigravityEnablesAndDisablesCleanly()
    {
        AntigravityBackend backend;
        backend.setEnabled(true);
        QVERIFY(backend.enabled());
        // Since no language server is running on the test machine, it falls back to activity
        QVERIFY(!backend.busy());
        QVERIFY(backend.status() != QStringLiteral("Disabled"));

        backend.setEnabled(false);
        QVERIFY(!backend.enabled());
        QVERIFY(!backend.busy());
        QVERIFY(!backend.hasReading());
        QCOMPARE(backend.percent(), 0);
        QCOMPARE(backend.status(), QStringLiteral("Disabled"));
    }
};

QTEST_MAIN(UsageBackendTest)
#include "usagebackendtest.moc"
