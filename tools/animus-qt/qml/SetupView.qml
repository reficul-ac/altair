import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    objectName: "setupView"
    contentWidth: availableWidth
    background: Rectangle { color: animusTheme.window }

    function ageText(ageS) {
        return ageS < 0 ? "UNK" : ageS.toFixed(ageS >= 10 ? 0 : 1) + " s"
    }

    function boolState(valid, value, trueText, falseText) {
        return valid ? (value ? trueText : falseText) : "UNK"
    }

    function fieldStateText(state) {
        if (state === "fresh")
            return "FRESH"
        if (state === "stale")
            return "STALE"
        if (state === "unsupported")
            return "UNSUPPORTED"
        return "UNK"
    }

    function batteryText() {
        if (vehicleModel.batteryRemainingValid)
            return vehicleModel.batteryRemainingPct + "%"
        if (vehicleModel.batteryVoltageValid)
            return vehicleModel.batteryVoltageV.toFixed(1) + " V"
        return root.fieldStateText(telemetryService.batteryFieldState)
    }

    function firmwareModeText() {
        if (!vehicleModel.heartbeatValid)
            return "UNK"
        return vehicleModel.autopilotLabel + " / " + vehicleModel.baseModeSummary
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

    function tileCacheSummary() {
        return mapCache.cachedTileCount + "/" + mapCache.totalTileCount +
               " cached, " + mapCache.missingTileCount + " missing, " +
               mapCache.failedTileCount + " failed"
    }

    function mapPolicyText() {
        if (offlineMaps.mode === 2)
            return "Strict offline"
        if (offlineMaps.mode === 1)
            return "Cached/offline"
        return "Online"
    }

    function providerStatusText() {
        let blocked = mapCache.providerBlockReason(mapCache.activeProviderId,
                                                   offlineMaps.networkAllowed)
        return blocked || "Active provider allowed"
    }

    function readinessSummary() {
        return "Link " + (telemetryService.linkFresh ? "fresh" : "stale") +
               " | Firmware " + root.firmwareModeText() +
               " | Battery " + root.batteryText() +
               " | Armed " + root.boolState(vehicleModel.heartbeatValid, vehicleModel.armed,
                                             "armed", "disarmed") +
               " | GPS " + root.gpsState() +
               " | Mission " + (vehicleModel.missionValid ? "seq " + vehicleModel.missionSeq : "UNK") +
               " | Home " + (vehicleModel.homeValid ? "set" : "UNK") +
               " | Terrain " + (vehicleModel.terrainValid ? "valid" : "UNK")
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        AnimusSetupSection {
            objectName: "setupReadinessSection"
            title: "Readiness"
            summary: root.readinessSummary()
            detailsLabel: "readiness details"

            GridLayout {
                Layout.fillWidth: true
                columns: 4
                rowSpacing: 8
                columnSpacing: 12

                Label { text: "Link"; color: animusTheme.mutedText; font.bold: true }
                AnimusStatusBadge {
                    objectName: "setupReadinessLinkBadge"
                    text: telemetryService.linkFresh ? "FRESH" : "STALE"
                    tone: telemetryService.linkFresh ? "success" : "warning"
                }

                Label { text: "Armed"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    objectName: "setupReadinessArmedState"
                    text: root.boolState(vehicleModel.heartbeatValid, vehicleModel.armed,
                                         "ARMED", "DISARMED")
                    color: vehicleModel.heartbeatValid && vehicleModel.armed ? animusTheme.warning : animusTheme.text
                    font.bold: true
                }

                Label { text: "GPS"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    objectName: "setupReadinessGpsState"
                    text: root.gpsState()
                    color: vehicleModel.gpsValid && vehicleModel.gpsFixType >= 3 ? animusTheme.success : animusTheme.warning
                    font.bold: true
                }

                Label { text: "Firmware"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    objectName: "setupReadinessFirmwareModeState"
                    Layout.fillWidth: true
                    text: root.firmwareModeText()
                    color: telemetryService.firmwareModeFieldState === "fresh"
                           ? animusTheme.text : animusTheme.warning
                    font.bold: true
                    elide: Text.ElideRight
                }

                Label { text: "Battery"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    objectName: "setupReadinessBatteryState"
                    text: root.batteryText()
                    color: telemetryService.batteryFieldState === "fresh"
                           ? animusTheme.text : animusTheme.warning
                    font.bold: true
                }

                Label { text: "Mission"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    objectName: "setupReadinessMissionState"
                    text: vehicleModel.missionValid ? "SEQ " + vehicleModel.missionSeq : "UNK"
                }

                Label { text: "Home"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    objectName: "setupReadinessHomeState"
                    Layout.fillWidth: true
                    text: vehicleModel.homeValid ? "SET" : "UNK"
                }

                Label { text: "Terrain"; color: animusTheme.mutedText; font.bold: true }
                Label {
                    objectName: "setupReadinessTerrainState"
                    text: vehicleModel.terrainValid
                          ? vehicleModel.terrainCurrentHeightM.toFixed(1) + " m AGL"
                          : "UNK"
                }
            }

            detailsContent: Component {
                GridLayout {
                    width: parent.width
                    columns: 4
                    rowSpacing: 6
                    columnSpacing: 12

                    Label { text: "Home position"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: vehicleModel.homeValid
                              ? vehicleModel.homeLatitudeDeg.toFixed(5) + ", " +
                                vehicleModel.homeLongitudeDeg.toFixed(5)
                              : "UNK"
                        elide: Text.ElideRight
                    }

                    Label { text: "Terrain report"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        text: vehicleModel.terrainValid
                              ? vehicleModel.terrainCurrentHeightM.toFixed(1) + " m AGL / " +
                                vehicleModel.terrainLoaded + " loaded"
                              : "UNK"
                    }

                    Label { text: "Firmware mode"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: root.firmwareModeText()
                        elide: Text.ElideRight
                    }

                    Label { text: "Battery"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        text: vehicleModel.batteryValid
                              ? root.batteryText() +
                                (vehicleModel.batteryCurrentValid
                                 ? " / " + vehicleModel.batteryCurrentA.toFixed(1) + " A"
                                 : "")
                              : root.fieldStateText(telemetryService.batteryFieldState)
                    }
                }
            }
        }

        AnimusSetupSection {
            objectName: "setupTelemetryLinkSection"
            title: "Telemetry Link"
            summary: (telemetryService.running ? "Receiver running" : "Receiver stopped") +
                     " | " + (telemetryService.linkFresh ? "fresh" : "stale") +
                     " | RX " + root.ageText(telemetryService.lastDatagramAgeS) +
                     " | decoded " + root.ageText(telemetryService.lastDecodedAgeS)
            detailsLabel: "link diagnostics"

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                AnimusStatusBadge {
                    objectName: "setupTelemetryRunningBadge"
                    text: telemetryService.running ? "RUNNING" : "STOPPED"
                    tone: telemetryService.running ? "success" : "warning"
                }

                AnimusStatusBadge {
                    objectName: "setupTelemetryFreshnessBadge"
                    text: telemetryService.linkFresh ? "FRESH" : "STALE"
                    tone: telemetryService.linkFresh ? "success" : "warning"
                }

                Label {
                    Layout.fillWidth: true
                    text: "RX " + root.ageText(telemetryService.lastDatagramAgeS) +
                          " / decoded " + root.ageText(telemetryService.lastDecodedAgeS)
                    color: animusTheme.mutedText
                    elide: Text.ElideRight
                }
            }

            detailsContent: Component {
                GridLayout {
                    width: parent.width
                    columns: 4
                    rowSpacing: 6
                    columnSpacing: 12

                    Label { text: "Endpoint"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: telemetryService.udpHost + ":" + telemetryService.udpPort
                        elide: Text.ElideRight
                    }

                    Label { text: "MAVLink ID"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        text: vehicleModel.heartbeatValid || telemetryService.decodedSampleCount > 0
                              ? vehicleModel.systemId + "." + vehicleModel.componentId
                              : "UNK"
                    }

                    Label { text: "Packet age"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        text: "RX " + root.ageText(telemetryService.lastDatagramAgeS) +
                              " / decoded " + root.ageText(telemetryService.lastDecodedAgeS)
                    }

                    Label { text: "Rates"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        text: "RX " + telemetryService.datagramRateHz.toFixed(1) +
                              " Hz / decoded " + telemetryService.decodedRateHz.toFixed(1) + " Hz"
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

                    Label { text: "Identity"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: vehicleModel.heartbeatValid
                              ? vehicleModel.vehicleTypeLabel + " / " + vehicleModel.autopilotLabel
                              : "UNK"
                        elide: Text.ElideRight
                    }

                    Label { text: "Field states"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: "Firmware " + telemetryService.firmwareModeFieldState +
                              " / battery " + telemetryService.batteryFieldState
                        elide: Text.ElideRight
                    }

                    Label { text: "Custom mode"; color: animusTheme.mutedText; font.bold: true }
                    Label { text: vehicleModel.heartbeatValid ? vehicleModel.customMode : "UNK" }

                    Label { text: "Base mode"; color: animusTheme.mutedText; font.bold: true }
                    Label { text: vehicleModel.heartbeatValid ? vehicleModel.baseMode : "UNK" }

                    Label { text: "MAV state"; color: animusTheme.mutedText; font.bold: true }
                    Label {
                        text: vehicleModel.heartbeatValid
                              ? vehicleModel.systemStatusLabel + " (" + vehicleModel.systemStatus + ")"
                              : "UNK"
                    }
                }
            }
        }

        AnimusSetupSection {
            objectName: "setupVehicleModelSection"
            title: "Vehicle Model"
            summary: vehicleModelProfiles.selectedProfile.name + " | " +
                     vehicleModelProfiles.surfaces.length + " mapped surfaces" +
                     (root.hasReversedSurface() ? " | custom polarity" : " | default polarity")
            detailsLabel: "model diagnostics"

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 8
                columnSpacing: 12

                Label { text: "Profile"; color: animusTheme.mutedText; font.bold: true }
                ComboBox {
                    id: modelProfileSelector
                    objectName: "setupModelProfileSelector"
                    Layout.fillWidth: true
                    model: vehicleModelProfiles.profiles
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: root.profileIndex(vehicleModelProfiles.selectedProfileId)
                    onActivated: vehicleModelProfiles.selectedProfileId = currentValue
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    objectName: "setupModelSurfaceSummary"
                    Layout.fillWidth: true
                    text: vehicleModelProfiles.surfaces.length + " mapped surfaces"
                    color: animusTheme.mutedText
                }
                Button {
                    objectName: "setupResetProfileDefaultsButton"
                    text: "Reset Profile Defaults"
                    enabled: root.hasReversedSurface()
                    onClicked: vehicleModelProfiles.resetAllSurfacePolarity()
                }
            }

            detailsContent: Component {
                ColumnLayout {
                    width: parent.width
                    spacing: 8

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 6
                        columnSpacing: 12

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

                    Repeater {
                        model: vehicleModelProfiles.surfaces
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: diagnosticSurfaceLayout.implicitHeight + 12
                            color: "transparent"
                            border.width: 1
                            border.color: animusTheme.border
                            radius: 4

                            GridLayout {
                                id: diagnosticSurfaceLayout
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
                                    objectName: "setupSurfacePolarityButton"
                                    text: root.polarityText(modelData.polarity)
                                    onClicked: vehicleModelProfiles.reverseSurfacePolarity(modelData.id)
                                }

                                Label {
                                    Layout.columnSpan: 2
                                    Layout.fillWidth: true
                                    text: modelData.valid
                                          ? modelData.deflectionDeg.toFixed(1) + " deg live deflection"
                                          : "Live deflection unknown"
                                    color: modelData.valid ? animusTheme.text : animusTheme.warning
                                    font.bold: true
                                }

                                Button {
                                    Layout.columnSpan: 2
                                    text: "Reset Surface"
                                    enabled: modelData.polarityReversed
                                    onClicked: vehicleModelProfiles.resetSurfacePolarity(modelData.id)
                                }

                                Label { text: "Channel"; color: animusTheme.mutedText }
                                Label { text: "CH " + modelData.actuatorChannel }

                                Label { text: "Node"; color: animusTheme.mutedText }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.node
                                    elide: Text.ElideMiddle
                                }

                                Label { text: "Polarity"; color: animusTheme.mutedText }
                                Label {
                                    text: "Profile " + modelData.profilePolarity +
                                          " / active " + modelData.polarity
                                }
                            }
                        }
                    }
                }
            }
        }

        AnimusSetupSection {
            objectName: "setupMapsTerrainSection"
            title: "Maps And Terrain"
            summary: root.mapPolicyText() + " | " + root.providerStatusText() +
                     " | " + root.tileCacheSummary()
            detailsLabel: "map and cache diagnostics"

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "Map Policy"
                    color: animusTheme.mutedText
                    font.bold: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    RadioButton {
                        objectName: "setupStrictOfflinePolicyButton"
                        text: "Strict offline"
                        checked: offlineMaps.mode === 2
                        onClicked: offlineMaps.mode = 2
                    }
                    RadioButton {
                        objectName: "setupCachedOfflinePolicyButton"
                        text: "Cached/offline"
                        checked: offlineMaps.mode === 1
                        onClicked: offlineMaps.mode = 1
                    }
                    RadioButton {
                        objectName: "setupOnlinePolicyButton"
                        text: "Online"
                        checked: offlineMaps.mode === 0
                        onClicked: offlineMaps.mode = 0
                    }
                }

                Label {
                    objectName: "setupMapProviderStatus"
                    Layout.fillWidth: true
                    text: root.providerStatusText()
                    color: root.providerStatusText() === "Active provider allowed"
                           ? animusTheme.mutedText
                           : animusTheme.warning
                    wrapMode: Text.WordWrap
                }

                Label {
                    text: "Providers"
                    color: animusTheme.mutedText
                    font.bold: true
                }

                Repeater {
                    model: mapCache
                    RadioDelegate {
                        objectName: "setupMapProviderButton"
                        Layout.fillWidth: true
                        text: label + " / " + typeLabel +
                              (networkRequired ? " - network" : " - local")
                        checked: mapCache.activeProviderId === providerId
                        enabled: mapCache.providerBlockReason(providerId,
                                                              offlineMaps.networkAllowed) === ""
                        onClicked: mapCache.activeProviderId = providerId
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        objectName: "setupTileCacheStatus"
                        Layout.fillWidth: true
                        text: "Tile cache: " + root.tileCacheSummary()
                        color: animusTheme.mutedText
                        elide: Text.ElideRight
                    }
                    ProgressBar {
                        objectName: "setupTileCacheProgress"
                        Layout.preferredWidth: 160
                        from: 0
                        to: 100
                        value: mapCache.progressPercent
                    }
                }

                ListView {
                    objectName: "setupTileSetList"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 190
                    model: mapCache.tileSets
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 58
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
                                    text: modelData.name
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.status + " | " + modelData.cachedCount + "/" +
                                          modelData.tileCount + " cached | " +
                                          modelData.missingCount + " missing | " +
                                          modelData.failedCount + " failed"
                                    color: modelData.status === "complete" ? animusTheme.success : animusTheme.warning
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                            }

                            Button {
                                objectName: "setupTileDownloadButton"
                                text: "Download"
                                enabled: modelData.status !== "downloading" &&
                                         offlineMaps.networkAllowed &&
                                         mapCache.providerBlockReason(modelData.providerId,
                                                                      true) === ""
                                onClicked: mapCache.downloadTileSet(modelData.id)
                            }

                            Button {
                                objectName: "setupTileCancelButton"
                                text: "Cancel"
                                enabled: modelData.status === "downloading"
                                onClicked: mapCache.cancelTileSetDownload(modelData.id)
                            }

                            Button {
                                objectName: "setupTileDeleteButton"
                                text: "Delete"
                                onClicked: mapCache.deleteTileSet(modelData.id)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        objectName: "setupSeedCacheButton"
                        text: "Seed Cruise 6DOF 5mi Offline Area"
                        onClicked: mapCache.ensureDefaultCruise6DofTileSet()
                    }
                    Button {
                        objectName: "setupCreateCacheButton"
                        text: "Create Cruise 6DOF 5mi Bounds Set"
                        onClicked: mapCache.createTileSet("Cruise 6DOF 5mi Origin",
                                                          -122.2607248, 37.3552151,
                                                          -122.0786752, 37.4997849, 12, 15)
                    }
                    Button {
                        objectName: "setupReloadCacheButton"
                        text: "Reload Cache"
                        onClicked: mapCache.reloadTileSets()
                    }
                }
            }

            detailsContent: Component {
                ColumnLayout {
                    width: parent.width
                    spacing: 8

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 6
                        columnSpacing: 12

                        Label { text: "Cache root"; color: animusTheme.mutedText; font.bold: true }
                        Label {
                            Layout.fillWidth: true
                            text: mapCache.rootPath
                            elide: Text.ElideMiddle
                        }

                        Label { text: "Cache DB"; color: animusTheme.mutedText; font.bold: true }
                        Label {
                            Layout.fillWidth: true
                            text: mapCache.cacheDatabasePath
                            elide: Text.ElideMiddle
                        }

                        Label { text: "Status"; color: animusTheme.mutedText; font.bold: true }
                        Label {
                            Layout.fillWidth: true
                            text: mapCache.lastError() || mapCache.activeStatus
                            color: mapCache.lastError() ? animusTheme.warning : animusTheme.mutedText
                            wrapMode: Text.WordWrap
                        }
                    }

                    Repeater {
                        model: mapCache.tileSets
                        delegate: Label {
                            Layout.fillWidth: true
                            text: modelData.id + " | provider " + modelData.providerId +
                                  " | z" + modelData.minZoom + "-" + modelData.maxZoom
                            color: animusTheme.mutedText
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        AnimusSetupSection {
            objectName: "setupLogsSection"
            title: "Logs"
            summary: "Qt onboard-log and session recording controls are not available in this build"
            detailsLabel: "log workflow status"

            Label {
                objectName: "setupLoggingStatus"
                Layout.fillWidth: true
                text: "Recording unavailable"
                color: animusTheme.mutedText
            }

            detailsContent: Component {
                Label {
                    width: parent.width
                    text: "Bridge-side .altlog recording and .tlog export are available from the Python live bridge; Qt Setup path persistence and onboard log controls remain future work."
                    color: animusTheme.mutedText
                    wrapMode: Text.WordWrap
                }
            }
        }

        AnimusSetupSection {
            objectName: "setupDiagnosticsSection"
            title: "Diagnostics"
            summary: "Raw link, cache, and model identifiers are available in each section detail"
            detailsLabel: "diagnostics index"

            Label {
                objectName: "setupDiagnosticsSummary"
                Layout.fillWidth: true
                text: "Expand section details for endpoint, counters, MAVLink IDs, profile IDs, GLB assets, nodes, cache paths, and tile-set IDs."
                color: animusTheme.mutedText
                wrapMode: Text.WordWrap
            }
        }
    }
}
