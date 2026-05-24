import QtQuick
import QtQuick.Controls

Label {
    id: root

    property string tone: "neutral"
    property color toneColor: {
        if (tone === "success")
            return animusTheme.success
        if (tone === "warning")
            return animusTheme.warning
        if (tone === "danger")
            return animusTheme.danger
        return animusTheme.mutedText
    }

    padding: 5
    leftPadding: 8
    rightPadding: 8
    color: root.toneColor
    font.pixelSize: 11
    font.bold: true
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    background: Rectangle {
        color: animusTheme.surface
        opacity: 0.86
        border.color: root.toneColor
        radius: 6
    }
}
