import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents3
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.plasmoid 2.0
import com.github.hivinz.codenotch 1.0

Item {
    id: root

    readonly property int visibleProviderCount: (claudeBackend.enabled ? 1 : 0)
                                                + (codexBackend.enabled ? 1 : 0)

    Layout.minimumWidth: Kirigami.Units.gridUnit * 18
    Layout.minimumHeight: Kirigami.Units.gridUnit * 10
    Layout.preferredWidth: Kirigami.Units.gridUnit * 22
    Layout.preferredHeight: Kirigami.Units.gridUnit * 14
    Plasmoid.switchWidth: Layout.minimumWidth
    Plasmoid.switchHeight: Layout.minimumHeight
    Plasmoid.backgroundHints: PlasmaCore.Types.DefaultBackground
    Plasmoid.preferredRepresentation: Plasmoid.formFactor === PlasmaCore.Types.Planar
                                      ? Plasmoid.fullRepresentation
                                      : Plasmoid.compactRepresentation

    function providerSummary(name, backend) {
        return backend.hasReading
            ? i18n("%1 %2% used", name, backend.percent)
            : i18n("%1: %2", name, backend.status)
    }

    ClaudeBackend {
        id: claudeBackend
        enabled: Plasmoid.configuration.claudeEnabled
    }

    UsageBackend {
        id: codexBackend
        enabled: Plasmoid.configuration.codexEnabled
    }

    Plasmoid.title: i18n("Kodenotch")
    Plasmoid.icon: "kodenotch"
    Plasmoid.toolTipMainText: i18n("Coding assistant usage")
    Plasmoid.toolTipSubText: providerSummary(i18n("Claude"), claudeBackend)
                              + "\n" + providerSummary(i18n("Codex"), codexBackend)

    Plasmoid.compactRepresentation: Item {
        implicitWidth: Kirigami.Units.gridUnit * 3
        implicitHeight: Kirigami.Units.gridUnit * 2 * Math.max(1, root.visibleProviderCount)

        Column {
            anchors.fill: parent

            UsageRing {
                backend: claudeBackend
                providerIcon: "kodenotch-claude"
                visible: backend.enabled
                width: parent.width
                height: parent.height / Math.max(1, root.visibleProviderCount)
            }

            UsageRing {
                backend: codexBackend
                providerIcon: "kodenotch-codex"
                visible: backend.enabled
                width: parent.width
                height: parent.height / Math.max(1, root.visibleProviderCount)
            }
        }

        PlasmaComponents3.Label {
            anchors.centerIn: parent
            visible: root.visibleProviderCount === 0
            text: "—"
        }

        MouseArea {
            anchors.fill: parent
            onClicked: Plasmoid.expanded = !Plasmoid.expanded
        }
    }

    Plasmoid.fullRepresentation: Item {
        Layout.minimumWidth: root.Layout.minimumWidth
        Layout.minimumHeight: root.Layout.minimumHeight
        Layout.preferredWidth: root.Layout.preferredWidth
        Layout.preferredHeight: Math.max(root.Layout.preferredHeight,
                                         content.implicitHeight + Kirigami.Units.largeSpacing * 2)

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            ProviderSection {
                providerName: i18n("Claude")
                backend: claudeBackend
                visible: backend.enabled
            }

            ProviderSection {
                providerName: i18n("Codex")
                backend: codexBackend
                visible: backend.enabled
            }

            PlasmaComponents3.Label {
                visible: root.visibleProviderCount === 0
                text: i18n("All integrations are disabled. Enable one in the widget settings.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                opacity: 0.8
            }
        }
    }
}
