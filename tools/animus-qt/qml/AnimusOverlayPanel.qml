import QtQuick
import QtQuick.Controls

Frame {
    id: root

    property color panelColor: animusTheme.overlay
    property color borderColor: animusTheme.border
    property real panelOpacity: 0.94
    property int cornerRadius: 6
    property int borderWidth: 1

    padding: 8
    background: Rectangle {
        color: root.panelColor
        opacity: root.panelOpacity
        border.color: root.borderColor
        border.width: root.borderWidth
        radius: root.cornerRadius
    }
}
