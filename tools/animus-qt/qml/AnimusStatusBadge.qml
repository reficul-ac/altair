import QtQuick
import QtQuick.Controls

Label {
    id: root

    property string tone: "neutral"
    property bool emphasized: tone === "warning" || tone === "danger"
    property color toneColor: {
        if (tone === "success")
            return animusTheme.success
        if (tone === "warning")
            return animusTheme.warning
        if (tone === "danger")
            return animusTheme.danger
        return animusTheme.mutedText
    }

    padding: 4
    leftPadding: 8
    rightPadding: 8
    color: root.tone === "neutral" ? animusTheme.mutedText : root.toneColor
    font.pixelSize: 11
    font.bold: true
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    background: Rectangle {
        color: root.emphasized ? animusTheme.surface : "transparent"
        border.color: root.tone === "neutral" ? animusTheme.border : root.toneColor
        border.width: root.tone === "neutral" ? 0 : 1
        opacity: root.emphasized ? 0.92 : 1.0
        radius: 4
    }
}
