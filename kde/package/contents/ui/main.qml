pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.plasmoid
import com.github.hivinz.codenotch

PlasmoidItem {
    id: root

    readonly property int visibleProviderCount: (claudeBackend.enabled ? 1 : 0)
                                                + (codexBackend.enabled ? 1 : 0)

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
    toolTipMainText: i18n("Coding assistant usage")
    toolTipSubText: providerSummary(i18n("Claude"), claudeBackend)
                        + "\n" + providerSummary(i18n("Codex"), codexBackend)

    compactRepresentation: Item {
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

        PlasmaComponents.Label {
            anchors.centerIn: parent
            visible: root.visibleProviderCount === 0
            text: "—"
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.expanded = !root.expanded
        }
    }

    fullRepresentation: Item {
        Layout.minimumWidth: Kirigami.Units.gridUnit * 18
        Layout.minimumHeight: Kirigami.Units.gridUnit * 10
        Layout.preferredWidth: Kirigami.Units.gridUnit * 22
        Layout.preferredHeight: Math.max(Kirigami.Units.gridUnit * 14,
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

            PlasmaComponents.Label {
                visible: root.visibleProviderCount === 0
                text: i18n("All integrations are disabled. Enable one in the widget settings.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                opacity: 0.8
            }
        }
    }

    component UsageRing: Item {
        id: ringItem
        required property var backend
        required property string providerIcon
        implicitWidth: Kirigami.Units.gridUnit * 3
        implicitHeight: Kirigami.Units.gridUnit * 2

        Kirigami.Icon {
            id: providerMark
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(8, Math.min(Kirigami.Units.iconSizes.small,
                                        parent.height * 0.5))
            height: width
            source: ringItem.providerIcon
        }

        Canvas {
            id: ring
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, Math.min(parent.height,
                                        parent.width - providerMark.width
                                        - Kirigami.Units.smallSpacing)
                               - Kirigami.Units.smallSpacing)
            height: width

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections {
                target: ringItem.backend
                function onReadingChanged() { ring.requestPaint() }
                function onEnabledChanged() { ring.requestPaint() }
            }
            onPaint: {
                const ctx = getContext("2d")
                const center = width / 2
                const radius = Math.max(0, center - 2)
                ctx.clearRect(0, 0, width, height)
                ctx.lineWidth = 3
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.18)
                ctx.beginPath()
                ctx.arc(center, center, radius, 0, Math.PI * 2)
                ctx.stroke()
                if (ringItem.backend.hasReading) {
                    ctx.strokeStyle = Kirigami.Theme.highlightColor
                    ctx.lineCap = "round"
                    ctx.beginPath()
                    ctx.arc(center, center, radius, -Math.PI / 2,
                            -Math.PI / 2 + Math.PI * 2 * ringItem.backend.percent / 100)
                    ctx.stroke()
                }
            }
        }

        PlasmaComponents.Label {
            anchors.centerIn: ring
            text: ringItem.backend.hasReading ? ringItem.backend.percent : "—"
            font.pixelSize: Math.max(7, ring.width * 0.24)
        }
    }

    component ProviderSection: ColumnLayout {
        id: section
        required property string providerName
        required property var backend
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            PlasmaComponents.Label {
                text: section.providerName
                font.bold: true
                Layout.fillWidth: true
            }
            PlasmaComponents.BusyIndicator {
                running: section.backend.busy
                visible: running
            }
            PlasmaComponents.ToolButton {
                icon.name: "view-refresh-symbolic"
                text: i18n("Refresh")
                enabled: !section.backend.busy
                onClicked: section.backend.refresh()
            }
        }

        Repeater {
            model: section.backend.windows
            delegate: ColumnLayout {
                id: windowRow
                required property var modelData
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    PlasmaComponents.Label {
                        text: windowRow.modelData.label
                        Layout.fillWidth: true
                    }
                    PlasmaComponents.Label {
                        text: i18n("%1% used", windowRow.modelData.percent)
                    }
                }
                PlasmaComponents.ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: windowRow.modelData.percent
                }
                PlasmaComponents.Label {
                    visible: windowRow.modelData.resetsAt !== undefined
                    text: visible
                        ? i18n("Resets %1", Qt.formatDateTime(windowRow.modelData.resetsAt,
                                                             "ddd HH:mm"))
                        : ""
                    opacity: 0.7
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                }
            }
        }

        PlasmaComponents.Label {
            visible: section.backend.windows.length === 0
            text: section.backend.status
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            opacity: 0.8
        }
    }
}
