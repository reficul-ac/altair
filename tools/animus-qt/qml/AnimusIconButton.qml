import QtQuick
import QtQuick.Controls

Button {
    id: root

    property string toolTipText: ""
    property color activeColor: animusTheme.accent

    width: 36
    height: 34
    padding: 0
    font.pixelSize: 16
    font.bold: true
    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText

    contentItem: Label {
        text: root.text
        color: root.checked ? root.activeColor
              : root.enabled ? animusTheme.text
              : animusTheme.mutedText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font: root.font
    }

    background: Rectangle {
        color: root.down ? animusTheme.window
              : root.checked ? animusTheme.surface
              : root.hovered ? animusTheme.overlay
              : "transparent"
        border.color: root.checked ? root.activeColor
                    : root.hovered ? animusTheme.border
                    : "transparent"
        border.width: root.checked || root.hovered ? 1 : 0
        radius: 4
    }
}
