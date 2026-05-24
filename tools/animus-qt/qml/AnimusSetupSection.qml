import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root

    property string title: ""
    property string summary: ""
    property string detailsLabel: "Details"
    property bool detailsExpanded: false
    property Component detailsContent: null
    default property alias content: body.data

    Layout.fillWidth: true
    padding: 12

    background: Rectangle {
        color: animusTheme.surface
        border.color: animusTheme.border
        border.width: 1
        radius: 6
    }

    ColumnLayout {
        width: parent.width
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: root.title
                color: animusTheme.text
                font.bold: true
                font.pixelSize: 15
                elide: Text.ElideRight
            }

            ToolButton {
                objectName: root.objectName + "DetailsToggle"
                visible: root.detailsContent !== null
                text: root.detailsExpanded ? "^" : "v"
                ToolTip.visible: hovered
                ToolTip.text: (root.detailsExpanded ? "Hide " : "Show ") + root.detailsLabel
                onClicked: root.detailsExpanded = !root.detailsExpanded
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.summary.length > 0
            text: root.summary
            color: animusTheme.mutedText
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 10
        }

        Loader {
            Layout.fillWidth: true
            active: root.detailsExpanded && root.detailsContent !== null
            sourceComponent: root.detailsContent
        }
    }
}
