import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "workspaceShell"

    readonly property var workspaceIds: ["map-2d", "terrain-3d", "fpv", "tactical", "setup"]
    property string currentWorkspace: workspaceIds[tabs.currentIndex]
    property bool diagnosticsDrawerOpen: false

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
            "themeMode": animusTheme.mode,
            "settingsDisclosure": root.itemDiagnostic(settingsButton, settingsButton.toolTipText),
            "diagnosticsDrawer": root.itemDiagnostic(diagnosticsDrawer, "diagnostics drawer"),
            "linkStatus": root.itemDiagnostic(linkStatusLabel, linkStatusLabel.text),
            "authority": root.itemDiagnostic(authorityLabel, authorityLabel.text),
            "tabs": [
                root.tabDiagnostic(map2DTab),
                root.tabDiagnostic(terrain3DTab),
                root.tabDiagnostic(fpvTab),
                root.tabDiagnostic(tacticalTab),
                root.tabDiagnostic(setupTab)
            ]
        }
    }

    function workspaceChromeDiagnosticsJson() {
        return JSON.stringify(root.workspaceChromeDiagnostics(), null, 2)
    }

    function linkStatusText() {
        if (!telemetryService.running && telemetryService.decodedSampleCount === 0)
            return "Link idle"
        return telemetryService.linkFresh ? "Link fresh" : "Link stale"
    }

    function linkStatusColor() {
        if (telemetryService.linkFresh)
            return animusTheme.success
        if (telemetryService.running || telemetryService.decodedSampleCount > 0)
            return animusTheme.warning
        return animusTheme.mutedText
    }

    function telemetryStatusText() {
        var source = telemetryService.running ? "RUNNING" : "STOPPED"
        var freshness = telemetryService.linkFresh ? "FRESH" : "STALE"
        return source + " / " + freshness + " / decoded " + telemetryService.decodedSampleCount
    }

    function currentCaptureText() {
        var item = workspaceStack.currentItem
        if (!item || item.lastCaptureOk === undefined)
            return "Capture state unavailable"
        if (item.lastCaptureOk)
            return "Last capture OK"
        if (item.lastCaptureError && item.lastCaptureError.length > 0)
            return item.lastCaptureError
        return "No capture in this session"
    }

    function currentSceneText() {
        var item = workspaceStack.currentItem
        if (!item || item.localSceneStatus === undefined)
            return "Scene diagnostics unavailable"
        var status = item.localSceneStatus.status || "unknown"
        if (item.useFallbackScene !== undefined && item.useFallbackScene())
            return status + " / QML fallback"
        return status
    }

    StackLayout {
        id: workspaceStack
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: chrome.bottom
        anchors.bottom: parent.bottom
        clip: true
        currentIndex: tabs.currentIndex

        Map2DView { id: map2DView }
        Terrain3DView { id: terrain3DView }
        FpvView { id: fpvView }
        TacticalAttitudeView { id: tacticalView }
        SetupView { id: setupView }
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
            background: Rectangle {
                color: animusTheme.surface
                border.color: animusTheme.border
            }
            RowLayout {
                anchors.fill: parent
                spacing: 10
                Label {
                    text: "Animus Qt"
                    color: animusTheme.text
                    font.pixelSize: 18
                    font.bold: true
                    Layout.leftMargin: 12
                }
                Label {
                    id: linkStatusLabel
                    objectName: "linkStatusLabel"
                    text: root.linkStatusText()
                    color: root.linkStatusColor()
                    font.bold: telemetryService.linkFresh
                    Layout.leftMargin: 10
                }
                Label {
                    id: authorityLabel
                    objectName: "commandAuthorityLabel"
                    text: "Read-only"
                    color: animusTheme.mutedText
                }
                Item { Layout.fillWidth: true }
                AnimusIconButton {
                    id: settingsButton
                    objectName: "headerSettingsButton"
                    text: "..."
                    toolTipText: "Diagnostics and settings"
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 34
                    Layout.rightMargin: 12
                    onClicked: root.diagnosticsDrawerOpen = !root.diagnosticsDrawerOpen
                }
            }
        }

        TabBar {
            id: tabs
            objectName: "workspaceTabs"
            Layout.fillWidth: true
            onCurrentIndexChanged: root.currentWorkspace = root.workspaceIds[currentIndex]
            background: Rectangle {
                color: animusTheme.window
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: animusTheme.border
                    opacity: 0.65
                }
            }
            AnimusWorkspaceTab {
                id: map2DTab
                objectName: "workspaceTabMap2D"
                text: "Map 2D"
            }
            AnimusWorkspaceTab {
                id: terrain3DTab
                objectName: "workspaceTabTerrain3D"
                text: "Terrain 3D"
            }
            AnimusWorkspaceTab {
                id: fpvTab
                objectName: "workspaceTabFpv"
                text: "FPV"
            }
            AnimusWorkspaceTab {
                id: tacticalTab
                objectName: "workspaceTabTactical"
                text: "Tactical"
            }
            AnimusWorkspaceTab {
                id: setupTab
                objectName: "workspaceTabSetup"
                text: "Setup"
            }
        }
    }

    Rectangle {
        id: diagnosticsDrawer
        objectName: "headerDiagnosticsDrawer"
        anchors.top: chrome.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Math.min(parent.width - 24, 380)
        visible: root.diagnosticsDrawerOpen
        z: 200
        color: animusTheme.overlay
        border.color: animusTheme.border
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Diagnostics"
                    color: animusTheme.text
                    font.pixelSize: 16
                    font.bold: true
                    Layout.fillWidth: true
                }
                AnimusIconButton {
                    text: "x"
                    toolTipText: "Close diagnostics"
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 28
                    onClicked: root.diagnosticsDrawerOpen = false
                }
            }

            AnimusSetupSection {
                title: "Telemetry"
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    rowSpacing: 6
                    columnSpacing: 12
                    Layout.fillWidth: true
                    Label { text: "Source"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        text: telemetryService.running ? "Active" : "Stopped"
                        color: telemetryService.running ? animusTheme.success : animusTheme.warning
                    }
                    Label { text: "Link"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        text: root.telemetryStatusText()
                        color: telemetryService.linkFresh ? animusTheme.success : animusTheme.warning
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        objectName: "headerMockTelemetryButton"
                        text: telemetryService.running ? "Stop" : "Mock"
                        Layout.fillWidth: true
                        onClicked: telemetryService.running ? telemetryService.stop()
                                                          : telemetryService.startMockTelemetry()
                    }
                    Button {
                        objectName: "headerUdpTelemetryButton"
                        text: "UDP"
                        enabled: !telemetryService.running
                        Layout.fillWidth: true
                        onClicked: telemetryService.startUdpTelemetry()
                    }
                }
            }

            AnimusSetupSection {
                title: "Display"
                Layout.fillWidth: true
                Button {
                    objectName: "headerThemeToggleButton"
                    text: "Theme: " + animusTheme.displayName
                    Layout.fillWidth: true
                    onClicked: animusTheme.toggleMode()
                }
            }

            AnimusSetupSection {
                title: "Scene"
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    rowSpacing: 6
                    columnSpacing: 12
                    Layout.fillWidth: true
                    Label { text: "WebEngine"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        text: webEngineTerrainEnabled ? "Enabled" : "Disabled"
                        color: webEngineTerrainEnabled ? animusTheme.success : animusTheme.warning
                    }
                    Label { text: "Workspace"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        text: root.currentSceneText()
                        color: animusTheme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            AnimusSetupSection {
                title: "Capture"
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: root.currentCaptureText()
                    color: root.currentCaptureText().indexOf("OK") >= 0
                           ? animusTheme.success : animusTheme.mutedText
                    wrapMode: Text.WordWrap
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
