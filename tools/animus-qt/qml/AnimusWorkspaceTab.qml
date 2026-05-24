import QtQuick
import QtQuick.Controls

TabButton {
    id: root

    implicitHeight: 38
    leftPadding: 14
    rightPadding: 14

    contentItem: Label {
        text: root.text
        color: root.checked ? animusTheme.text : animusTheme.mutedText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.bold: root.checked
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: root.checked ? animusTheme.surface
              : root.hovered ? animusTheme.overlay
              : animusTheme.window
        border.color: root.checked ? animusTheme.accent : "transparent"
        border.width: root.checked ? 1 : 0

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.checked ? 2 : 1
            color: root.checked ? animusTheme.accent : animusTheme.border
            opacity: root.checked ? 1.0 : 0.55
        }
    }
}
