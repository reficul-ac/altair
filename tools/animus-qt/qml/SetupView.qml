import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root

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

                Label { text: "Endpoint"; color: "#4b5563"; font.bold: true }
                Label {
                    text: telemetryService.udpHost + ":" + telemetryService.udpPort
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label { text: "State"; color: "#4b5563"; font.bold: true }
                Label {
                    text: (telemetryService.running ? "RUNNING" : "STOPPED") + " / " +
                          (telemetryService.linkFresh ? "FRESH" : "STALE")
                    color: telemetryService.linkFresh ? "#0f7b43" : "#7a4b00"
                    font.bold: true
                }

                Label { text: "Datagrams"; color: "#4b5563"; font.bold: true }
                Label { text: telemetryService.datagramCount }
                Label { text: "Decoded"; color: "#4b5563"; font.bold: true }
                Label { text: telemetryService.decodedSampleCount }

                Label { text: "Decode errors"; color: "#4b5563"; font.bold: true }
                Label {
                    text: telemetryService.decodeErrorCount
                    color: telemetryService.decodeErrorCount > 0 ? "#7a4b00" : "#202020"
                }
                Label { text: "Ages"; color: "#4b5563"; font.bold: true }
                Label {
                    text: "RX " + root.ageText(telemetryService.lastDatagramAgeS) +
                          " / decoded " + root.ageText(telemetryService.lastDecodedAgeS)
                }

                Label { text: "MAVLink ID"; color: "#4b5563"; font.bold: true }
                Label {
                    text: vehicleModel.heartbeatValid || telemetryService.decodedSampleCount > 0
                          ? vehicleModel.systemId + "." + vehicleModel.componentId
                          : "UNK"
                }
                Label { text: "Armed"; color: "#4b5563"; font.bold: true }
                Label {
                    text: root.boolState(vehicleModel.heartbeatValid, vehicleModel.armed,
                                         "ARMED", "DISARMED")
                    color: vehicleModel.heartbeatValid && vehicleModel.armed ? "#7a2f00" : "#202020"
                }

                Label { text: "GPS"; color: "#4b5563"; font.bold: true }
                Label { text: root.gpsState() }
                Label { text: "Mission"; color: "#4b5563"; font.bold: true }
                Label { text: vehicleModel.missionValid ? "SEQ " + vehicleModel.missionSeq : "UNK" }

                Label { text: "Home"; color: "#4b5563"; font.bold: true }
                Label {
                    text: vehicleModel.homeValid
                          ? vehicleModel.homeLatitudeDeg.toFixed(5) + ", " +
                            vehicleModel.homeLongitudeDeg.toFixed(5)
                          : "UNK"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label { text: "Terrain"; color: "#4b5563"; font.bold: true }
                Label {
                    text: vehicleModel.terrainValid
                          ? vehicleModel.terrainCurrentHeightM.toFixed(1) + " m AGL / " +
                            vehicleModel.terrainLoaded + " loaded"
                          : "UNK"
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
                           ? "#4b5563"
                           : "#7a4b00"
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
                    color: "#4b5563"
                }
                Label {
                    text: mapCache.lastError() || mapCache.activeStatus
                    color: mapCache.lastError() ? "#7a4b00" : "#4b5563"
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
                                    color: modelData.status === "complete" ? "#0f7b43" : "#7a4b00"
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
