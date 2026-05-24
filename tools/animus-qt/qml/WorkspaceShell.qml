import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "workspaceShell"

    readonly property var workspaceIds: ["map-2d", "terrain-3d", "fpv", "tactical", "setup"]
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
            "themeMode": animusTheme.mode,
            "settingsDisclosure": root.itemDiagnostic(settingsButton, settingsButton.toolTipText),
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
        FpvView {}
        TacticalAttitudeView {}
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
                    toolTipText: "Telemetry and display settings"
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 34
                    Layout.rightMargin: 12
                    onClicked: settingsPopup.open()
                }
                Popup {
                    id: settingsPopup
                    objectName: "headerSettingsPopup"
                    x: Math.max(8, settingsButton.x + settingsButton.width - width)
                    y: settingsButton.y + settingsButton.height + 6
                    width: 320
                    padding: 0
                    modal: false
                    focus: true
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    background: Rectangle {
                        color: animusTheme.overlay
                        border.color: animusTheme.border
                        radius: 6
                    }
                    contentItem: ColumnLayout {
                        spacing: 10

                        Label {
                            text: "Header Settings"
                            color: animusTheme.text
                            font.bold: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Layout.topMargin: 12
                        }
                        GridLayout {
                            columns: 2
                            rowSpacing: 6
                            columnSpacing: 12
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Layout.fillWidth: true

                            Label {
                                text: "Endpoint"
                                color: animusTheme.mutedText
                                font.bold: true
                            }
                            Label {
                                text: telemetryService.udpHost + ":" + telemetryService.udpPort
                                color: animusTheme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: "State"
                                color: animusTheme.mutedText
                                font.bold: true
                            }
                            Label {
                                text: root.telemetryStatusText()
                                color: telemetryService.linkFresh ? animusTheme.success : animusTheme.warning
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Button {
                            objectName: "headerThemeToggleButton"
                            text: "Theme: " + animusTheme.displayName
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Layout.fillWidth: true
                            onClicked: animusTheme.toggleMode()
                        }
                        Button {
                            objectName: "headerMockTelemetryButton"
                            text: telemetryService.running ? "Stop Telemetry" : "Start Mock Telemetry"
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Layout.fillWidth: true
                            onClicked: telemetryService.running ? telemetryService.stop()
                                                              : telemetryService.startMockTelemetry()
                        }
                        Button {
                            objectName: "headerUdpTelemetryButton"
                            text: "Start UDP Telemetry"
                            enabled: !telemetryService.running
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Layout.bottomMargin: 12
                            Layout.fillWidth: true
                            onClicked: telemetryService.startUdpTelemetry()
                        }
                    }
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
                border.color: animusTheme.border
            }
            TabButton {
                id: map2DTab
                objectName: "workspaceTabMap2D"
                text: "Map 2D"
                contentItem: Label {
                    text: map2DTab.text
                    color: map2DTab.checked ? animusTheme.text : animusTheme.mutedText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.bold: map2DTab.checked
                }
                background: Rectangle {
                    color: map2DTab.checked ? animusTheme.surface : animusTheme.window
                    border.color: animusTheme.border
                }
            }
            TabButton {
                id: terrain3DTab
                objectName: "workspaceTabTerrain3D"
                text: "Terrain 3D"
                contentItem: Label {
                    text: terrain3DTab.text
                    color: terrain3DTab.checked ? animusTheme.text : animusTheme.mutedText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.bold: terrain3DTab.checked
                }
                background: Rectangle {
                    color: terrain3DTab.checked ? animusTheme.surface : animusTheme.window
                    border.color: animusTheme.border
                }
            }
            TabButton {
                id: fpvTab
                objectName: "workspaceTabFpv"
                text: "FPV"
                contentItem: Label {
                    text: fpvTab.text
                    color: fpvTab.checked ? animusTheme.text : animusTheme.mutedText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.bold: fpvTab.checked
                }
                background: Rectangle {
                    color: fpvTab.checked ? animusTheme.surface : animusTheme.window
                    border.color: animusTheme.border
                }
            }
            TabButton {
                id: tacticalTab
                objectName: "workspaceTabTactical"
                text: "Tactical"
                contentItem: Label {
                    text: tacticalTab.text
                    color: tacticalTab.checked ? animusTheme.text : animusTheme.mutedText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.bold: tacticalTab.checked
                }
                background: Rectangle {
                    color: tacticalTab.checked ? animusTheme.surface : animusTheme.window
                    border.color: animusTheme.border
                }
            }
            TabButton {
                id: setupTab
                objectName: "workspaceTabSetup"
                text: "Setup"
                contentItem: Label {
                    text: setupTab.text
                    color: setupTab.checked ? animusTheme.text : animusTheme.mutedText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.bold: setupTab.checked
                }
                background: Rectangle {
                    color: setupTab.checked ? animusTheme.surface : animusTheme.window
                    border.color: animusTheme.border
                }
            }
        }
    }
}
