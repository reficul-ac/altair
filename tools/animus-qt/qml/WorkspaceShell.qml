import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "workspaceShell"

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

    function itemDiagnostic(item, label) {
        var topLeft = item.mapToItem(root, 0, 0)
        return {
            "objectName": item.objectName,
            "label": label,
            "visible": item.visible,
            "enabled": item.enabled === undefined ? true : item.enabled,
            "opacity": item.opacity,
            "x": Math.round(topLeft.x),
            "y": Math.round(topLeft.y),
            "width": Math.round(item.width),
            "height": Math.round(item.height),
            "semanticallyVisible": item.visible && item.opacity > 0.01 &&
                                   item.width > 1 && item.height > 1
        }
    }

    function tabDiagnostic(tab) {
        var diagnostic = root.itemDiagnostic(tab, tab.text)
        var labelItem = tab.contentItem
        if (labelItem) {
            var labelDiagnostic = root.itemDiagnostic(labelItem, labelItem.text || tab.text)
            diagnostic["labelItem"] = labelDiagnostic
            diagnostic["labelTextMatches"] = labelDiagnostic.label === tab.text
            diagnostic["labelInsideTab"] =
                    labelDiagnostic.x >= diagnostic.x &&
                    labelDiagnostic.y >= diagnostic.y &&
                    labelDiagnostic.x + labelDiagnostic.width <= diagnostic.x + diagnostic.width &&
                    labelDiagnostic.y + labelDiagnostic.height <= diagnostic.y + diagnostic.height
        } else {
            diagnostic["labelItem"] = null
            diagnostic["labelTextMatches"] = false
            diagnostic["labelInsideTab"] = false
        }
        return diagnostic
    }

    function workspaceChromeDiagnostics() {
        return {
            "selectedWorkspace": root.currentWorkspace,
            "currentIndex": tabs.currentIndex,
            "chrome": root.itemDiagnostic(chrome, "chrome"),
            "tabs": [
                root.tabDiagnostic(map2DTab),
                root.tabDiagnostic(terrain3DTab),
                root.tabDiagnostic(setupTab)
            ]
        }
    }

    function workspaceChromeDiagnosticsJson() {
        return JSON.stringify(root.workspaceChromeDiagnostics(), null, 2)
    }

    StackLayout {
        id: workspaceStack
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: chrome.bottom
        anchors.bottom: parent.bottom
        clip: true
        currentIndex: tabs.currentIndex

        Map2DView {}
        Terrain3DView {}
        SetupView {}
    }

    ColumnLayout {
        id: chrome
        objectName: "workspaceChrome"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 0
        z: 100

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
            objectName: "workspaceTabs"
            Layout.fillWidth: true
            onCurrentIndexChanged: root.currentWorkspace = root.workspaceIds[currentIndex]
            TabButton {
                id: map2DTab
                objectName: "workspaceTabMap2D"
                text: "Map 2D"
            }
            TabButton {
                id: terrain3DTab
                objectName: "workspaceTabTerrain3D"
                text: "Terrain 3D"
            }
            TabButton {
                id: setupTab
                objectName: "workspaceTabSetup"
                text: "Setup"
            }
        }
    }
}
