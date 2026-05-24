import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    background: Rectangle { color: animusTheme.window }

    function ageText(ageS) {
        return ageS < 0 ? "UNK" : ageS.toFixed(ageS >= 10 ? 0 : 1) + " s"
    }

    function boolState(valid, value, trueText, falseText) {
        return valid ? (value ? trueText : falseText) : "UNK"
    }

    function gpsState() {
        if (!vehicleModel.gpsValid)
            return "UNK"
        if (vehicleModel.gpsFixType < 3)
            return "NO FIX"
        return "FIX " + vehicleModel.gpsFixType + " / " +
               vehicleModel.satellitesVisible + " sats"
    }

    function profileIndex(profileId) {
        for (let index = 0; index < vehicleModelProfiles.profiles.length; ++index) {
            if (vehicleModelProfiles.profiles[index].id === profileId)
                return index
        }
        return 0
    }

    function polarityText(value) {
        return value < 0 ? "Reversed" : "Normal"
    }

    function hasReversedSurface() {
        for (let index = 0; index < vehicleModelProfiles.surfaces.length; ++index) {
            if (vehicleModelProfiles.surfaces[index].polarityReversed)
                return true
        }
        return false
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        GroupBox {
            title: "Telemetry Link"
            Layout.fillWidth: true
            GridLayout {
                columns: 4
                rowSpacing: 6
                columnSpacing: 16

                Label { text: "Endpoint"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    text: telemetryService.udpHost + ":" + telemetryService.udpPort
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label { text: "State"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    text: (telemetryService.running ? "RUNNING" : "STOPPED") + " / " +
                          (telemetryService.linkFresh ? "FRESH" : "STALE")
                    color: telemetryService.linkFresh ? animusTheme.success : animusTheme.warning
                    font.bold: true
                }

                Label { text: "Datagrams"; color: animusTheme.mutedText; font.bold: true }
                Label { text: telemetryService.datagramCount }
                Label { text: "Decoded"; color: animusTheme.mutedText; font.bold: true }
                Label { text: telemetryService.decodedSampleCount }

                Label { text: "Decode errors"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    text: telemetryService.decodeErrorCount
                    color: telemetryService.decodeErrorCount > 0 ? animusTheme.warning : animusTheme.text
                }
                Label { text: "Ages"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    text: "RX " + root.ageText(telemetryService.lastDatagramAgeS) +
                          " / decoded " + root.ageText(telemetryService.lastDecodedAgeS)
                }

                Label { text: "MAVLink ID"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    text: vehicleModel.heartbeatValid || telemetryService.decodedSampleCount > 0
                          ? vehicleModel.systemId + "." + vehicleModel.componentId
                          : "UNK"
                }
                Label { text: "Armed"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    text: root.boolState(vehicleModel.heartbeatValid, vehicleModel.armed,
                                         "ARMED", "DISARMED")
                    color: vehicleModel.heartbeatValid && vehicleModel.armed ? animusTheme.warning : animusTheme.text
                }

                Label { text: "GPS"; color: animusTheme.mutedText; font.bold: true }
                Label { text: root.gpsState() }
                Label { text: "Mission"; color: animusTheme.mutedText; font.bold: true }
                Label { text: vehicleModel.missionValid ? "SEQ " + vehicleModel.missionSeq : "UNK" }

                Label { text: "Home"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    text: vehicleModel.homeValid
                          ? vehicleModel.homeLatitudeDeg.toFixed(5) + ", " +
                            vehicleModel.homeLongitudeDeg.toFixed(5)
                          : "UNK"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label { text: "Terrain"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    text: vehicleModel.terrainValid
                          ? vehicleModel.terrainCurrentHeightM.toFixed(1) + " m AGL / " +
                            vehicleModel.terrainLoaded + " loaded"
                          : "UNK"
                }
            }
        }

        GroupBox {
            title: "Terrain 3D Vehicle Model"
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: 10

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 6
                    columnSpacing: 12

                    Label { text: "Profile"; color: animusTheme.mutedText; font.bold: true }
                    ComboBox {
                        id: modelProfileSelector
                        Layout.fillWidth: true
                        model: vehicleModelProfiles.profiles
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: root.profileIndex(vehicleModelProfiles.selectedProfileId)
                        onActivated: vehicleModelProfiles.selectedProfileId = currentValue
                    }

                    Label { text: "Profile ID"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: vehicleModelProfiles.selectedProfile.id
                        elide: Text.ElideRight
                    }

                    Label { text: "GLB asset"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: vehicleModelProfiles.selectedProfile.asset
                        elide: Text.ElideMiddle
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: vehicleModelProfiles.surfaces.length + " mapped surfaces"
                        color: animusTheme.mutedText
                    }
                    Button {
                        text: "Reset Profile Defaults"
                        enabled: root.hasReversedSurface()
                        onClicked: vehicleModelProfiles.resetAllSurfacePolarity()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: animusTheme.border
                }

                Repeater {
                    model: vehicleModelProfiles.surfaces
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: surfaceLayout.implicitHeight + 12
                        color: "transparent"
                        border.width: 1
                        border.color: animusTheme.border
                        radius: 4

                        GridLayout {
                            id: surfaceLayout
                            anchors.fill: parent
                            anchors.margins: 6
                            columns: 2
                            rowSpacing: 4
                            columnSpacing: 10

                            Label {
                                Layout.fillWidth: true
                                text: modelData.label + " (" + modelData.id + ")"
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Button {
                                text: root.polarityText(modelData.polarity)
                                onClicked: vehicleModelProfiles.reverseSurfacePolarity(modelData.id)
                            }

                            RowLayout {
                                Layout.columnSpan: 2
                                Layout.fillWidth: true
                                spacing: 10

                                Label {
                                    text: "CH " + modelData.actuatorChannel
                                    color: animusTheme.mutedText
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.node
                                    color: animusTheme.mutedText
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    text: modelData.valid
                                          ? modelData.deflectionDeg.toFixed(1) + " deg"
                                          : "UNK"
                                    color: modelData.valid ? animusTheme.text : animusTheme.warning
                                    font.bold: true
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Profile polarity " + modelData.profilePolarity +
                                      " / active " + modelData.polarity
                                color: animusTheme.mutedText
                                elide: Text.ElideRight
                            }
                            Button {
                                text: "Reset Surface"
                                enabled: modelData.polarityReversed
                                onClicked: vehicleModelProfiles.resetSurfacePolarity(modelData.id)
                            }
                        }
                    }
                }
            }
        }

        GroupBox {
            title: "Map Policy"
            Layout.fillWidth: true
            ColumnLayout {
                RowLayout {
                    RadioButton {
                        text: "Strict offline"
                        checked: offlineMaps.mode === 2
                        onClicked: offlineMaps.mode = 2
                    }
                    RadioButton {
                        text: "Cached/offline"
                        checked: offlineMaps.mode === 1
                        onClicked: offlineMaps.mode = 1
                    }
                    RadioButton {
                        text: "Online"
                        checked: offlineMaps.mode === 0
                        onClicked: offlineMaps.mode = 0
                    }
                }
                Label {
                    text: mapCache.providerBlockReason(mapCache.activeProviderId,
                                                       offlineMaps.networkAllowed) ||
                          "Active provider allowed by current policy"
                    color: mapCache.providerBlockReason(mapCache.activeProviderId,
                                                        offlineMaps.networkAllowed) === ""
                           ? animusTheme.mutedText
                           : animusTheme.warning
                }
            }
        }

        GroupBox {
            title: "Map Providers"
            Layout.fillWidth: true
            ColumnLayout {
                Repeater {
                    model: mapCache
                    RadioDelegate {
                        Layout.fillWidth: true
                        text: label + " / " + typeLabel +
                              (networkRequired ? " - network" : " - local")
                        checked: mapCache.activeProviderId === providerId
                        enabled: mapCache.providerBlockReason(providerId,
                                                              offlineMaps.networkAllowed) === ""
                        onClicked: mapCache.activeProviderId = providerId
                    }
                }
            }
        }

        GroupBox {
            title: "Offline Tile Cache"
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                Label { text: "Root: " + mapCache.rootPath }
                Label { text: "Database: " + mapCache.cacheDatabasePath; elide: Text.ElideRight }
                Label {
                    text: "Tiles: " + mapCache.cachedTileCount + "/" + mapCache.totalTileCount +
                          " cached | " + mapCache.missingTileCount + " missing | " +
                          mapCache.failedTileCount + " failed | " +
                          mapCache.inFlightTileCount + " in flight"
                    color: animusTheme.mutedText
                }
                Label {
                    text: mapCache.lastError() || mapCache.activeStatus
                    color: mapCache.lastError() ? animusTheme.warning : animusTheme.mutedText
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: mapCache.progressPercent
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 220
                    model: mapCache.tileSets
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 54
                        color: "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.name + " (" + modelData.id + ")"
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.status + " | " + modelData.cachedCount + "/" +
                                          modelData.tileCount + " cached | " +
                                          modelData.missingCount + " missing | " +
                                          modelData.failedCount + " failed | z" +
                                          modelData.minZoom + "-" + modelData.maxZoom
                                    color: modelData.status === "complete" ? animusTheme.success : animusTheme.warning
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                            }

                            Button {
                                text: "Download"
                                enabled: modelData.status !== "downloading" &&
                                         offlineMaps.networkAllowed &&
                                         mapCache.providerBlockReason(modelData.providerId,
                                                                      true) === ""
                                onClicked: mapCache.downloadTileSet(modelData.id)
                            }

                            Button {
                                text: "Cancel"
                                enabled: modelData.status === "downloading"
                                onClicked: mapCache.cancelTileSetDownload(modelData.id)
                            }

                            Button {
                                text: "Delete"
                                onClicked: mapCache.deleteTileSet(modelData.id)
                            }
                        }
                    }
                }
                Button {
                    text: "Seed Cruise 6DOF 5mi Offline Area"
                    onClicked: mapCache.ensureDefaultCruise6DofTileSet()
                }
                Button {
                    text: "Create Cruise 6DOF 5mi Bounds Set"
                    onClicked: mapCache.createTileSet("Cruise 6DOF 5mi Origin",
                                                      -122.2607248, 37.3552151,
                                                      -122.0786752, 37.4997849, 12, 15)
                }
                Button {
                    text: "Reload Cache"
                    onClicked: mapCache.reloadTileSets()
                }
            }
        }
    }
}
