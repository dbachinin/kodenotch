#include "codenotchplugin.h"
#include "claudebackend.h"
#include "usagebackend.h"

#include <qqml.h>

void CodenotchPlugin::registerTypes(const char *uri)
{
    Q_ASSERT(QByteArray(uri) == QByteArrayLiteral("com.github.hivinz.codenotch"));
    qmlRegisterType<ClaudeBackend>(uri, 1, 0, "ClaudeBackend");
    qmlRegisterType<UsageBackend>(uri, 1, 0, "UsageBackend");
}
