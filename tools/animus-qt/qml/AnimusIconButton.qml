import QtQuick
import QtQuick.Controls

Button {
    id: root

    property string toolTipText: ""

    width: 36
    height: 34
    padding: 0
    font.pixelSize: 16
    font.bold: true
    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText
}
