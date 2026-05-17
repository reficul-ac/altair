import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    ColumnLayout {
        width: parent.width
        spacing: 12

        GroupBox {
            title: "Map Policy"
            Layout.fillWidth: true
            RowLayout {
                RadioButton {
                    text: "Strict offline"
                    checked: offlineMaps.mode === 2
                    onClicked: offlineMaps.mode = 2
                }
                RadioButton {
                    text: "Cached/offline"
                    checked: offlineMaps.mode === 1
                    onClicked: offlineMaps.mode = 1
                }
                RadioButton {
                    text: "Online"
                    checked: offlineMaps.mode === 0
                    onClicked: offlineMaps.mode = 0
                }
            }
        }

        GroupBox {
            title: "Map Packs"
            Layout.fillWidth: true
            ColumnLayout {
                Label { text: "Root: " + mapPacks.rootPath }
                Label { text: mapPacks.validationError() }
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 220
                    model: mapPacks
                    delegate: RadioDelegate {
                        width: ListView.view.width
                        text: name + " (" + packId + ")"
                        checked: mapPacks.activePackId === packId
                        onClicked: mapPacks.activePackId = packId
                    }
                }
                Button {
                    text: "Reload Packs"
                    onClicked: mapPacks.reload()
                }
            }
        }
    }
}
