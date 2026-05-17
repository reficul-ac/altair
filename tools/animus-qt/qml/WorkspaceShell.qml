import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    readonly property var workspaceIds: ["map-2d", "terrain-3d", "setup"]
    property string currentWorkspace: workspaceIds[tabs.currentIndex]

    function selectWorkspace(workspaceId) {
        var index = workspaceIds.indexOf(workspaceId)
        if (index < 0)
            return false
        tabs.currentIndex = index
        currentWorkspace = workspaceId
        return true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                Label {
                    text: "Animus Qt"
                    font.pixelSize: 18
                    font.bold: true
                    Layout.leftMargin: 12
                }
                Label {
                    text: vehicleModel.connected ? "Telemetry live" : "Telemetry idle"
                    color: vehicleModel.connected ? "#0f7b43" : "#7a4b00"
                    Layout.leftMargin: 16
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: telemetryService.running ? "Stop" : "Mock Telemetry"
                    onClicked: telemetryService.running ? telemetryService.stop() : telemetryService.startMockTelemetry()
                }
                Button {
                    text: "UDP"
                    enabled: !telemetryService.running
                    onClicked: telemetryService.startUdpTelemetry()
                }
            }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            onCurrentIndexChanged: root.currentWorkspace = root.workspaceIds[currentIndex]
            TabButton { text: "Map 2D" }
            TabButton { text: "Terrain 3D" }
            TabButton { text: "Setup" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Map2DView {}
            Terrain3DView {}
            SetupView {}
        }
    }
}
