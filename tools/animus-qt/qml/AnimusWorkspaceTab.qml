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
        radius: 4
    }
}
