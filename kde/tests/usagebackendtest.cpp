#include "usagebackend.h"
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
};

QTEST_MAIN(UsageBackendTest)
#include "usagebackendtest.moc"
