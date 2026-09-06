import QtQuick 2.15
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents3

Item {
    id: root

    property var backend
    property string providerIcon
    implicitWidth: Kirigami.Units.gridUnit * 3
    implicitHeight: Kirigami.Units.gridUnit * 2

    Kirigami.Icon {
        id: providerMark
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(8, Math.min(Kirigami.Units.iconSizes.small,
                                    parent.height * 0.5))
        height: width
        source: root.providerIcon
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
            target: root.backend
            function onReadingChanged() { ring.requestPaint() }
        }

        onPaint: {
            const context = getContext("2d")
            const center = width / 2
            const radius = Math.max(0, center - 2)
            context.clearRect(0, 0, width, height)
            context.lineWidth = 3
            context.strokeStyle = Qt.rgba(1, 1, 1, 0.18)
            context.beginPath()
            context.arc(center, center, radius, 0, Math.PI * 2)
            context.stroke()
            if (root.backend && root.backend.hasReading) {
                context.strokeStyle = Kirigami.Theme.highlightColor
                context.lineCap = "round"
                context.beginPath()
                context.arc(center, center, radius, -Math.PI / 2,
                            -Math.PI / 2 + Math.PI * 2 * root.backend.percent / 100)
                context.stroke()
            }
        }
    }

    PlasmaComponents3.Label {
        anchors.centerIn: ring
        text: root.backend && root.backend.hasReading ? root.backend.percent : "—"
        font.pixelSize: Math.max(7, ring.width * 0.24)
    }
}
