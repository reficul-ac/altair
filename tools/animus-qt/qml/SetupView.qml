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
                    text: offlineMaps.sourceBlockReason(mapSources.activeSourceId) ||
                          "Active source allowed by current policy"
                    color: offlineMaps.canUseSource(mapSources.activeSourceId) ? "#4b5563" : "#7a4b00"
                }
            }
        }

        GroupBox {
            title: "Map Sources"
            Layout.fillWidth: true
            ColumnLayout {
                Repeater {
                    model: mapSources
                    RadioDelegate {
                        Layout.fillWidth: true
                        text: label + " [" + provider + "]" +
                              (networkRequired ? " - network" : " - local")
                        checked: mapSources.activeSourceId === sourceId
                        enabled: offlineMaps.canUseSource(sourceId)
                        onClicked: mapSources.activeSourceId = sourceId
                    }
                }
            }
        }

        GroupBox {
            title: "Map Packs"
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                Label { text: "Root: " + mapPacks.rootPath }
                Label { text: mapPacks.validationError() }
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 220
                    model: mapPacks
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 54
                        color: mapPacks.activePackId === packId ? "#fbfbf8" : "transparent"
                        property string imageryStatusText:
                            imagerySourceStatus.indexOf("placeholder") >= 0
                            ? "placeholder imagery"
                            : (imagerySourceStatus.length > 0
                               ? "real offline imagery"
                               : "imagery source unknown")

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: 8

                            RadioButton {
                                checked: mapPacks.activePackId === packId
                                onClicked: mapPacks.activePackId = packId
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                Label {
                                    Layout.fillWidth: true
                                    text: name + " (" + packId + ")"
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: imageryStatusText
                                    color: imageryStatusText.indexOf("placeholder") >= 0
                                           ? "#7a4b00"
                                           : "#0f7b43"
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: mapPacks.activePackId = packId
                        }
                    }
                }
                Button {
                    text: "Reload Packs"
                    onClicked: mapPacks.reload()
                }
            }
        }
    }
}
