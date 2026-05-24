import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    property var segments: []
    property string currentValue: ""
    property int segmentWidth: 74
    property int segmentHeight: 34

    signal selected(string value)

    spacing: 0

    Repeater {
        model: root.segments

        Button {
            id: segmentButton

            required property var modelData

            Layout.preferredWidth: root.segmentWidth
            Layout.preferredHeight: root.segmentHeight
            checkable: true
            checked: root.currentValue === modelData.value
            text: modelData.label
            onClicked: root.selected(modelData.value)

            contentItem: Label {
                text: segmentButton.text
                color: segmentButton.checked ? animusTheme.text : animusTheme.mutedText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 12
                font.bold: segmentButton.checked
                elide: Text.ElideRight
            }

            background: Rectangle {
                color: segmentButton.checked ? animusTheme.surface
                      : segmentButton.hovered ? animusTheme.overlay
                      : animusTheme.window
                border.color: segmentButton.checked ? animusTheme.accent : animusTheme.border
                border.width: segmentButton.checked ? 1 : 0

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: segmentButton.checked ? 2 : 1
                    color: segmentButton.checked ? animusTheme.accent : animusTheme.border
                    opacity: segmentButton.checked ? 1.0 : 0.45
                }
            }
        }
    }
}
