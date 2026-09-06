import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents3

ColumnLayout {
    id: root

    property string providerName
    property var backend
    Layout.fillWidth: true
    spacing: Kirigami.Units.smallSpacing

    RowLayout {
        Layout.fillWidth: true

        PlasmaComponents3.Label {
            text: root.providerName
            font.bold: true
            Layout.fillWidth: true
        }

        PlasmaComponents3.BusyIndicator {
            running: root.backend && root.backend.busy
            visible: running
        }

        PlasmaComponents3.ToolButton {
            icon.name: "view-refresh-symbolic"
            text: i18n("Refresh")
            enabled: root.backend && !root.backend.busy
            onClicked: root.backend.refresh()
        }
    }

    Repeater {
        model: root.backend ? root.backend.windows : []

        delegate: ColumnLayout {
            width: parent ? parent.width : 0

            RowLayout {
                Layout.fillWidth: true

                PlasmaComponents3.Label {
                    text: modelData.label
                    Layout.fillWidth: true
                }

                PlasmaComponents3.Label {
                    text: i18n("%1% used", modelData.percent)
                }
            }

            PlasmaComponents3.ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 100
                value: modelData.percent
            }

            PlasmaComponents3.Label {
                visible: modelData.resetsAt !== undefined
                text: visible
                    ? i18n("Resets %1", Qt.formatDateTime(modelData.resetsAt, "ddd HH:mm"))
                    : ""
                opacity: 0.7
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
            }
        }
    }

    PlasmaComponents3.Label {
        visible: !root.backend || root.backend.windows.length === 0
        text: root.backend ? root.backend.status : ""
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        opacity: 0.8
    }
}
