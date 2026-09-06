pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.plasmoid
import com.github.hivinz.codenotch

PlasmoidItem {
    id: root

    readonly property int visibleProviderCount: (antigravityBackend.enabled ? 1 : 0)
                                                + (claudeBackend.enabled ? 1 : 0)
                                                + (codexBackend.enabled ? 1 : 0)

    function providerSummary(name, backend) {
        return backend.hasReading
            ? i18n("%1 %2% used", name, backend.percent)
            : i18n("%1: %2", name, backend.status)
    }

    AntigravityBackend {
        id: antigravityBackend
        enabled: Plasmoid.configuration.antigravityEnabled
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
    toolTipSubText: providerSummary(i18n("Antigravity"), antigravityBackend)
                        + "\n" + providerSummary(i18n("Claude"), claudeBackend)
                        + "\n" + providerSummary(i18n("Codex"), codexBackend)

    compactRepresentation: Item {
        implicitWidth: Math.max(Kirigami.Units.gridUnit * 2,
                                antigravityRing.visible ? antigravityRing.implicitWidth : 0,
                                claudeRing.visible ? claudeRing.implicitWidth : 0,
                                codexRing.visible ? codexRing.implicitWidth : 0)
        implicitHeight: Kirigami.Units.gridUnit * 2 * Math.max(1, root.visibleProviderCount)
        // Without a width hint a horizontal panel squares the applet off and
        // clips the legend.
        Layout.minimumWidth: implicitWidth
        Layout.preferredWidth: implicitWidth

        Column {
            anchors.fill: parent

            UsageRing {
                id: antigravityRing
                backend: antigravityBackend
                providerIcon: "kodenotch-antigravity"
                providerName: i18n("Antigravity")
                visible: backend.enabled
                height: parent.height / Math.max(1, root.visibleProviderCount)
            }

            UsageRing {
                id: claudeRing
                backend: claudeBackend
                providerIcon: "kodenotch-claude"
                providerName: i18n("Claude")
                visible: backend.enabled
                height: parent.height / Math.max(1, root.visibleProviderCount)
            }

            UsageRing {
                id: codexRing
                backend: codexBackend
                providerIcon: "kodenotch-codex"
                providerName: i18n("Codex")
                visible: backend.enabled
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
                providerName: i18n("Antigravity")
                backend: antigravityBackend
                visible: backend.enabled
            }

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

    // The provider glyph inside a ring: a dim track with a coloured arc that
    // starts at 12 o'clock and sweeps clockwise by the fraction used, as on the
    // macOS notch. The legend to the right names the provider and what was used:
    // a percent of the limit, or a bare request count when that is all there is.
    component UsageRing: Item {
        id: ringItem
        required property var backend
        required property string providerIcon
        required property string providerName
        readonly property bool hasReading: backend.hasReading
        readonly property int percent: hasReading ? backend.percent : 0
        readonly property int requestsToday: backend.requestsToday ? backend.requestsToday : 0
        // Same thresholds and colours as UsageBand / Palette in the macOS app.
        readonly property color arcColor: percent < 50 ? "#00ff88" : percent < 70 ? "#f2ff00" : "#ff3f00"
        readonly property color trackColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                                    Kirigami.Theme.textColor.b, 0.18)

        implicitWidth: ring.width + Kirigami.Units.smallSpacing + legend.implicitWidth
        implicitHeight: Kirigami.Units.gridUnit * 2

        onHasReadingChanged: ring.requestPaint()
        onPercentChanged: ring.requestPaint()
        onTrackColorChanged: ring.requestPaint()

        Canvas {
            id: ring
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: ringItem.height
            height: width
            onWidthChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                const center = width / 2
                // Track and arc widths keep the macOS proportions (15.5 and 8 of 117).
                const trackWidth = Math.max(2, width * 0.13)
                const radius = Math.max(0, center - trackWidth / 2)
                ctx.clearRect(0, 0, width, height)
                ctx.lineWidth = trackWidth
                ctx.strokeStyle = ringItem.trackColor
                ctx.beginPath()
                ctx.arc(center, center, radius, 0, Math.PI * 2)
                ctx.stroke()
                if (!ringItem.hasReading)
                    return
                ctx.lineWidth = Math.max(1.5, width * 0.07)
                ctx.strokeStyle = ringItem.arcColor
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.arc(center, center, radius, -Math.PI / 2,
                        -Math.PI / 2 + Math.PI * 2 * Math.min(ringItem.percent, 100) / 100)
                ctx.stroke()
            }
        }

        Kirigami.Icon {
            anchors.centerIn: ring
            width: Math.max(6, ring.width * 0.45)
            height: width
            source: ringItem.providerIcon
            // A spent limit dims its glyph so the ring reads as "waiting".
            opacity: ringItem.hasReading && ringItem.percent >= 100 ? 0.35 : 1
        }

        PlasmaComponents.Label {
            id: legend
            anchors.left: ring.right
            anchors.leftMargin: Kirigami.Units.smallSpacing
            anchors.verticalCenter: parent.verticalCenter
            text: ringItem.providerName + " " + (ringItem.hasReading ? ringItem.percent + "%"
                                                 : ringItem.requestsToday > 0 ? i18n("%1 req", ringItem.requestsToday) : "—")
            font.pixelSize: Math.max(7, Math.min(Kirigami.Theme.defaultFont.pixelSize, ringItem.height * 0.75))
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
            PlasmaComponents.Label {
                visible: section.backend && section.backend.hasReading
                text: i18n("%1% used", section.backend ? section.backend.percent : 0)
                opacity: 0.85
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
