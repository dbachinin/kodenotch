import QtQuick 2.15
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents3

// The provider glyph inside a ring: a dim track with a coloured arc that
// starts at 12 o'clock and sweeps clockwise by the fraction used, as on the
// macOS notch. The legend to the right names the provider and what was used:
// a percent of the limit, or a bare request count when that is all there is.
Item {
    id: root

    property var backend
    property string providerIcon
    property string providerName
    readonly property bool hasReading: backend ? backend.hasReading : false
    readonly property int percent: hasReading ? backend.percent : 0
    readonly property int requestsToday: backend && backend.requestsToday ? backend.requestsToday : 0
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
        width: root.height
        height: width
        onWidthChanged: requestPaint()

        onPaint: {
            const context = getContext("2d")
            const center = width / 2
            // Track and arc widths keep the macOS proportions (15.5 and 8 of 117).
            const trackWidth = Math.max(2, width * 0.13)
            const radius = Math.max(0, center - trackWidth / 2)
            context.clearRect(0, 0, width, height)
            context.lineWidth = trackWidth
            context.strokeStyle = root.trackColor
            context.beginPath()
            context.arc(center, center, radius, 0, Math.PI * 2)
            context.stroke()
            if (!root.hasReading)
                return
            context.lineWidth = Math.max(1.5, width * 0.07)
            context.strokeStyle = root.arcColor
            context.lineCap = "round"
            context.beginPath()
            context.arc(center, center, radius, -Math.PI / 2,
                        -Math.PI / 2 + Math.PI * 2 * Math.min(root.percent, 100) / 100)
            context.stroke()
        }
    }

    Kirigami.Icon {
        anchors.centerIn: ring
        width: Math.max(6, ring.width * 0.45)
        height: width
        source: root.providerIcon
        // A spent limit dims its glyph so the ring reads as "waiting".
        opacity: root.hasReading && root.percent >= 100 ? 0.35 : 1
    }

    PlasmaComponents3.Label {
        id: legend
        anchors.left: ring.right
        anchors.leftMargin: Kirigami.Units.smallSpacing
        anchors.verticalCenter: parent.verticalCenter
        text: root.providerName + " " + (root.hasReading ? root.percent + "%"
                                         : root.requestsToday > 0 ? i18n("%1 req", root.requestsToday) : "—")
        font.pixelSize: Math.max(7, Math.min(Kirigami.Theme.defaultFont.pixelSize, root.height * 0.75))
    }
}
