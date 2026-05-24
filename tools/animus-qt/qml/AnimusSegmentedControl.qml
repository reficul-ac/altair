import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    property var segments: []
    property string currentValue: ""
    property int segmentWidth: 74

    signal selected(string value)

    spacing: 0

    Repeater {
        model: root.segments

        Button {
            required property var modelData

            Layout.preferredWidth: root.segmentWidth
            Layout.preferredHeight: 34
            checkable: true
            checked: root.currentValue === modelData.value
            text: modelData.label
            onClicked: root.selected(modelData.value)
        }
    }
}
