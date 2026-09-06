import QtQuick 2.15
import QtQuick.Controls 2.15 as QQC2
import org.kde.kirigami 2.20 as Kirigami

Kirigami.FormLayout {
    property alias cfg_antigravityEnabled: antigravityEnabled.checked
    property alias cfg_claudeEnabled: claudeEnabled.checked
    property alias cfg_codexEnabled: codexEnabled.checked

    QQC2.CheckBox {
        id: antigravityEnabled
        Kirigami.FormData.label: i18n("Antigravity:")
        text: i18n("Read usage from the local Antigravity language server")
    }

    QQC2.CheckBox {
        id: claudeEnabled
        Kirigami.FormData.label: i18n("Claude:")
        text: i18n("Read usage from Claude Code's private credential file")
    }

    QQC2.CheckBox {
        id: codexEnabled
        Kirigami.FormData.label: i18n("Codex:")
        text: i18n("Read usage from the local Codex app server")
    }

    QQC2.Label {
        Kirigami.FormData.isSection: true
        text: i18n("Turning an integration off cancels its work and discards its reading.")
        wrapMode: Text.Wrap
    }
}
