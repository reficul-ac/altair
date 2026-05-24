import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property bool following: true
    property bool mapDetailsExpanded: false
    property bool mapWarningExpanded: false
    property bool mapWarningDismissed: false
    property double manualCenterLatitudeDeg: vehicleModel.latitudeDeg
    property double manualCenterLongitudeDeg: vehicleModel.longitudeDeg
    property int zoomLevel: 15

    readonly property string providerBlockMessage:
        mapCache.providerBlockReason(mapCache.activeProviderId, offlineMaps.networkAllowed)
    readonly property bool providerAllowed:
        providerBlockMessage === ""
    readonly property double centerLatitudeDeg: following ? vehicleModel.latitudeDeg
                                                          : manualCenterLatitudeDeg
    readonly property double centerLongitudeDeg: following ? vehicleModel.longitudeDeg
                                                           : manualCenterLongitudeDeg
    readonly property double degreesPerPixel: 0.000014 * Math.pow(2, 15 - zoomLevel)
    readonly property int tileSize: 256

    function clampLatitude(latitudeDeg) {
        return Math.max(-85.05112878, Math.min(85.05112878, latitudeDeg))
    }

    function wrappedLongitude(longitudeDeg) {
        var wrapped = ((longitudeDeg + 180.0) % 360.0 + 360.0) % 360.0 - 180.0
        return wrapped === -180.0 ? 180.0 : wrapped
    }

    function clampZoom(zoom) {
        return Math.max(3, Math.min(22, zoom))
    }

    function projectX(longitudeDeg) {
        return width / 2 + root.longitudeToPixelX(longitudeDeg, zoomLevel) -
               root.longitudeToPixelX(centerLongitudeDeg, zoomLevel)
    }

    function projectY(latitudeDeg) {
        return height / 2 + root.latitudeToPixelY(latitudeDeg, zoomLevel) -
               root.latitudeToPixelY(centerLatitudeDeg, zoomLevel)
    }

    function longitudeToPixelX(longitudeDeg, zoom) {
        var n = Math.pow(2, zoom)
        return (wrappedLongitude(longitudeDeg) + 180.0) / 360.0 * n * tileSize
    }

    function latitudeToPixelY(latitudeDeg, zoom) {
        var latitudeRad = clampLatitude(latitudeDeg) * Math.PI / 180.0
        var n = Math.pow(2, zoom)
        return (1.0 - Math.log(Math.tan(latitudeRad) + 1.0 / Math.cos(latitudeRad)) /
                Math.PI) / 2.0 * n * tileSize
    }

    function visibleTileModel() {
        var tiles = []
        var n = Math.pow(2, zoomLevel)
        var centerX = longitudeToPixelX(centerLongitudeDeg, zoomLevel)
        var centerY = latitudeToPixelY(centerLatitudeDeg, zoomLevel)
        var firstX = Math.floor((centerX - width / 2) / tileSize)
        var lastX = Math.floor((centerX + width / 2) / tileSize)
        var firstY = Math.max(0, Math.floor((centerY - height / 2) / tileSize))
        var lastY = Math.min(n - 1, Math.floor((centerY + height / 2) / tileSize))
        for (var tileX = firstX; tileX <= lastX; ++tileX) {
            var wrappedX = ((tileX % n) + n) % n
            for (var tileY = firstY; tileY <= lastY; ++tileY) {
                var url = mapCache.tileUrlFor(mapCache.activeProviderId, zoomLevel, wrappedX,
                                              tileY, offlineMaps.networkAllowed)
                tiles.push({
                               "x": tileX * tileSize - (centerX - width / 2),
                               "y": tileY * tileSize - (centerY - height / 2),
                               "url": url
                           })
            }
        }
        return tiles
    }

    function defaultTileSetStatus() {
        for (var i = 0; i < mapCache.tileSets.length; ++i) {
            if (mapCache.tileSets[i].id === "cruise6dof-5mi-origin")
                return mapCache.tileSets[i].status + " cache | " +
                       mapCache.tileSets[i].cachedCount + "/" +
                       mapCache.tileSets[i].tileCount + " cached | z" +
                       mapCache.tileSets[i].minZoom + "-" + mapCache.tileSets[i].maxZoom
        }
        return "default offline area not initialized"
    }

    function conciseTileSetStatus() {
        for (var i = 0; i < mapCache.tileSets.length; ++i) {
            if (mapCache.tileSets[i].id === "cruise6dof-5mi-origin")
                return mapCache.tileSets[i].status + " " +
                       mapCache.tileSets[i].cachedCount + "/" +
                       mapCache.tileSets[i].tileCount
        }
        return "no seeded area"
    }

    function mapDetailText() {
        return "Provider: " + mapCache.activeProviderId + "\n" +
               "Map type: " + mapCache.activeMapTypeId + "\n" +
               "Cache tile set: " + root.defaultTileSetStatus() + "\n" +
               "Cache DB: " + (mapCache.cacheDatabasePath || "none") + "\n" +
               "Attribution: " + root.attributionText()
    }

    function syncManualCenterToVehicle() {
        manualCenterLatitudeDeg = clampLatitude(vehicleModel.latitudeDeg)
        manualCenterLongitudeDeg = wrappedLongitude(vehicleModel.longitudeDeg)
    }

    function recenterOnVehicle() {
        syncManualCenterToVehicle()
        following = true
    }

    function panByPixels(deltaX, deltaY) {
        following = false
        manualCenterLongitudeDeg = wrappedLongitude(manualCenterLongitudeDeg - deltaX *
                                                    degreesPerPixel)
        manualCenterLatitudeDeg = clampLatitude(manualCenterLatitudeDeg + deltaY *
                                                degreesPerPixel)
    }

    function zoomBy(delta) {
        var nextZoom = clampZoom(zoomLevel + delta)
        if (nextZoom === zoomLevel)
            return
        zoomLevel = nextZoom
    }

    function scaleMeters() {
        var latitudeRad = clampLatitude(centerLatitudeDeg) * Math.PI / 180.0
        var metersPerPixel = 156543.03392 * Math.cos(latitudeRad) / Math.pow(2, zoomLevel)
        return Math.max(1, Math.round(metersPerPixel * 96))
    }

    function scaleLabel() {
        var meters = scaleMeters()
        if (meters >= 1000)
            return (meters / 1000.0).toFixed(meters >= 10000 ? 0 : 1) + " km"
        return meters + " m"
    }

    function statusText() {
        if (root.providerBlockMessage)
            return root.providerBlockMessage
        return mapCache.activeMapTypeId + " | " + root.conciseTileSetStatus()
    }

    function attributionText() {
        return mapCache.activeAttribution || "Map attribution unavailable"
    }

    function metersPerPixel() {
        var latitudeRad = clampLatitude(centerLatitudeDeg) * Math.PI / 180.0
        return 156543.03392 * Math.cos(latitudeRad) / Math.pow(2, zoomLevel)
    }

    function severityColor(severity) {
        if (severity === "warning")
            return animusTheme.danger
        if (severity === "caution")
            return animusTheme.warning
        return animusTheme.mutedText
    }

    function overlayDiagnostics() {
        return {
            "missionItems": navigationOverlays.missionItemList().length,
            "geofences": navigationOverlays.geofenceList().length,
            "rallyPoints": navigationOverlays.rallyPointList().length,
            "eventMarkers": navigationOverlays.eventMarkerList().length,
            "breadcrumbs": breadcrumbModel.count,
            "home": vehicleModel.homeValid ? 1 : 0,
            "activeMissionSeq": vehicleModel.missionSeq,
            "missionValid": vehicleModel.missionValid
        }
    }

    function resetMapWarning() {
        mapWarningDismissed = false
        mapWarningExpanded = false
    }

    Connections {
        target: vehicleModel
        function onPositionChanged() {
            if (root.following)
                root.syncManualCenterToVehicle()
        }
    }

    Connections {
        target: mapCache
        function onActiveProviderChanged() { root.resetMapWarning() }
        function onStatusChanged() { root.resetMapWarning() }
    }

    Connections {
        target: offlineMaps
        function onModeChanged() { root.resetMapWarning() }
    }

    Connections {
        target: navigationOverlays.missionItems
        function onRowsInserted() { overlayCanvas.requestPaint() }
        function onRowsRemoved() { overlayCanvas.requestPaint() }
        function onModelReset() { overlayCanvas.requestPaint() }
    }

    Connections {
        target: navigationOverlays.geofences
        function onRowsInserted() { overlayCanvas.requestPaint() }
        function onRowsRemoved() { overlayCanvas.requestPaint() }
        function onModelReset() { overlayCanvas.requestPaint() }
    }

    Rectangle {
        anchors.fill: parent
        color: animusTheme.mapBackground

        Repeater {
            model: 16
            Rectangle {
                width: parent.width
                height: 1
                y: index * parent.height / 15
                color: animusTheme.mapGrid
            }
        }

        Repeater {
            model: 24
            Rectangle {
                width: 1
                height: parent.height
                x: index * parent.width / 23
                color: animusTheme.mapGrid
            }
        }

        Rectangle {
            x: parent.width * 0.08
            y: parent.height * 0.58
            width: parent.width * 0.84
            height: 28
            radius: 14
            rotation: -11
            color: animusTheme.mapLand
            opacity: 0.85
        }

        Rectangle {
            x: parent.width * 0.2
            y: parent.height * 0.22
            width: parent.width * 0.44
            height: 22
            radius: 11
            rotation: 31
            color: animusTheme.mapLandAlt
            opacity: 0.8
        }
    }

    Repeater {
        model: root.visibleTileModel()
        delegate: Image {
            x: modelData.x
            y: modelData.y
            width: root.tileSize
            height: root.tileSize
            source: modelData.url
            visible: modelData.url !== ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
        }
    }

    Canvas {
        id: overlayCanvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var geofences = navigationOverlays.geofenceList()
            for (var g = 0; g < geofences.length; ++g) {
                var fence = geofences[g]
                if (!fence.enabled)
                    continue
                ctx.strokeStyle = "rgba(213, 155, 40, 0.95)"
                ctx.fillStyle = "rgba(213, 155, 40, 0.16)"
                ctx.lineWidth = 2
                if (fence.type === "polygon" && fence.vertices.length >= 3) {
                    ctx.beginPath()
                    for (var v = 0; v < fence.vertices.length; ++v) {
                        var vertex = fence.vertices[v]
                        var x = root.projectX(vertex.longitudeDeg)
                        var y = root.projectY(vertex.latitudeDeg)
                        if (v === 0)
                            ctx.moveTo(x, y)
                        else
                            ctx.lineTo(x, y)
                    }
                    ctx.closePath()
                    ctx.fill()
                    ctx.stroke()
                } else if (fence.type === "circle") {
                    var radiusPx = Math.max(8, fence.radiusM / Math.max(0.1, root.metersPerPixel()))
                    ctx.beginPath()
                    ctx.arc(root.projectX(fence.centerLongitudeDeg),
                            root.projectY(fence.centerLatitudeDeg), radiusPx, 0, Math.PI * 2)
                    ctx.fill()
                    ctx.stroke()
                }
            }

            var mission = navigationOverlays.missionItemList()
            if (mission.length >= 2) {
                ctx.strokeStyle = "rgba(29, 111, 214, 0.82)"
                ctx.lineWidth = 3
                ctx.beginPath()
                for (var i = 0; i < mission.length; ++i) {
                    var item = mission[i]
                    var mx = root.projectX(item.longitudeDeg)
                    var my = root.projectY(item.latitudeDeg)
                    if (i === 0)
                        ctx.moveTo(mx, my)
                    else
                        ctx.lineTo(mx, my)
                }
                ctx.stroke()
            }
        }

        Connections {
            target: root
            function onCenterLatitudeDegChanged() { overlayCanvas.requestPaint() }
            function onCenterLongitudeDegChanged() { overlayCanvas.requestPaint() }
            function onZoomLevelChanged() { overlayCanvas.requestPaint() }
            function onWidthChanged() { overlayCanvas.requestPaint() }
            function onHeightChanged() { overlayCanvas.requestPaint() }
        }
        Connections {
            target: animusTheme
            function onThemeChanged() { overlayCanvas.requestPaint() }
        }
    }

    Repeater {
        model: navigationOverlays.missionItems
        delegate: Rectangle {
            width: 24
            height: 24
            radius: 12
            x: root.projectX(longitudeDeg) - width / 2
            y: root.projectY(latitudeDeg) - height / 2
            color: active ? animusTheme.accent : animusTheme.surface
            border.color: animusTheme.accent
            border.width: 2

            Label {
                anchors.centerIn: parent
                text: sequence
                color: active ? "white" : animusTheme.accent
                font.bold: true
            }
        }
    }

    Repeater {
        model: navigationOverlays.rallyPoints
        delegate: Rectangle {
            width: 18
            height: 18
            radius: 3
            rotation: 45
            x: root.projectX(longitudeDeg) - width / 2
            y: root.projectY(latitudeDeg) - height / 2
            visible: valid
            color: animusTheme.success
            border.color: animusTheme.surface
            border.width: 2
        }
    }

    Repeater {
        model: navigationOverlays.eventMarkers
        delegate: Rectangle {
            width: 16
            height: 16
            radius: 8
            x: root.projectX(longitudeDeg) - width / 2
            y: root.projectY(latitudeDeg) - height / 2
            visible: positionValid
            color: root.severityColor(severity)
            border.color: animusTheme.surface
            border.width: 2
        }
    }

    Repeater {
        model: breadcrumbModel
        delegate: Rectangle {
            width: 7
            height: 7
            radius: 3.5
            x: root.projectX(longitude) - width / 2
            y: root.projectY(latitude) - height / 2
            color: animusTheme.accent
            opacity: 0.42
        }
    }

    Rectangle {
        width: 24
        height: 24
        radius: 12
        x: root.projectX(vehicleModel.homeLongitudeDeg) - width / 2
        y: root.projectY(vehicleModel.homeLatitudeDeg) - height / 2
        color: animusTheme.surface
        border.color: animusTheme.danger
        border.width: 2

        Label {
            anchors.centerIn: parent
            text: "H"
            color: animusTheme.danger
            font.bold: true
        }
    }

    Item {
        width: 42
        height: 42
        x: root.projectX(vehicleModel.longitudeDeg) - width / 2
        y: root.projectY(vehicleModel.latitudeDeg) - height / 2
        rotation: vehicleModel.headingDeg

        Canvas {
            id: ownshipCanvas
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.beginPath()
                ctx.moveTo(width / 2, 3)
                ctx.lineTo(width - 5, height - 5)
                ctx.lineTo(width / 2, height - 13)
                ctx.lineTo(5, height - 5)
                ctx.closePath()
                ctx.fillStyle = animusTheme.accent
                ctx.strokeStyle = animusTheme.surface
                ctx.lineWidth = 3
                ctx.fill()
                ctx.stroke()
            }
        }
        Connections {
            target: animusTheme
            function onThemeChanged() { ownshipCanvas.requestPaint() }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        property real lastX: 0
        property real lastY: 0

        onPressed: function(mouse) {
            lastX = mouse.x
            lastY = mouse.y
        }
        onPositionChanged: function(mouse) {
            if ((pressedButtons & Qt.LeftButton) === 0)
                return
            root.panByPixels(mouse.x - lastX, mouse.y - lastY)
            lastX = mouse.x
            lastY = mouse.y
        }
        onWheel: function(wheel) {
            root.zoomBy(wheel.angleDelta.y > 0 ? 1 : -1)
            wheel.accepted = true
        }
    }

    AnimusOverlayPanel {
        id: mapSourcePanel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        width: Math.min(parent.width - 24, root.mapDetailsExpanded ? 620 : 560)
        borderColor: root.providerAllowed ? animusTheme.border : animusTheme.warning

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                AnimusStatusBadge {
                    text: offlineMaps.modeLabel()
                    tone: offlineMaps.networkAllowed ? "success" : "warning"
                }
                ComboBox {
                    id: providerSelector
                    Layout.preferredWidth: 210
                    model: mapCache
                    textRole: "label"
                    currentIndex: mapCache.providerIndex(mapCache.activeProviderId)
                    delegate: ItemDelegate {
                        width: providerSelector.width
                        enabled: mapCache.providerBlockReason(providerId,
                                                              offlineMaps.networkAllowed) === ""
                        text: label + " / " + typeLabel
                    }
                    onActivated: function(index) {
                        var selectedId = mapCache.providerIdAt(index)
                        if (mapCache.providerBlockReason(selectedId,
                                                         offlineMaps.networkAllowed) === "")
                            mapCache.activeProviderId = selectedId
                    }
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: root.statusText()
                    color: root.providerAllowed ? animusTheme.mutedText : animusTheme.warning
                    font.bold: !root.providerAllowed
                }
                AnimusIconButton {
                    text: root.mapDetailsExpanded ? "\u25b2" : "\u25bc"
                    toolTipText: root.mapDetailsExpanded ? "Hide map source details"
                                                           : "Show map source details"
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 28
                    onClicked: root.mapDetailsExpanded = !root.mapDetailsExpanded
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                AnimusIconButton {
                    text: "\u2212"
                    toolTipText: "Zoom out"
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 32
                    enabled: root.zoomLevel > root.clampZoom(root.zoomLevel - 1)
                    onClicked: root.zoomBy(-1)
                }
                Label {
                    text: "z" + root.zoomLevel
                    horizontalAlignment: Text.AlignHCenter
                    Layout.preferredWidth: 42
                }
                AnimusIconButton {
                    text: "+"
                    toolTipText: "Zoom in"
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 32
                    enabled: root.zoomLevel < root.clampZoom(root.zoomLevel + 1)
                    onClicked: root.zoomBy(1)
                }
                AnimusIconButton {
                    text: "\u21ba"
                    toolTipText: "Recenter on vehicle"
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 32
                    onClicked: root.recenterOnVehicle()
                }
                AnimusStatusBadge {
                    text: root.following ? "Following vehicle" : "Manual pan"
                    tone: root.following ? "success" : "warning"
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.scaleLabel()
                    color: animusTheme.mutedText
                }
                Rectangle {
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 6
                    color: animusTheme.text
                }
            }

            Label {
                Layout.fillWidth: true
                visible: root.mapDetailsExpanded
                text: root.mapDetailText()
                color: animusTheme.mutedText
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
        }
    }

    TelemetryStrip {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
    }

    Rectangle {
        id: mapWarning
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 88
        width: Math.min(parent.width - 40, root.mapWarningExpanded ? 590 : 390)
        height: root.mapWarningExpanded ? 190 : 54
        visible: !root.providerAllowed && !root.mapWarningDismissed
        z: 4
        color: animusTheme.surface
        border.color: animusTheme.warning
        border.width: 1
        radius: 8

        RowLayout {
            id: warningHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            height: 34
            spacing: 8
            z: 1

            Rectangle {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                radius: 12
                color: animusTheme.warning
                Label {
                    anchors.centerIn: parent
                    text: "!"
                    color: animusTheme.window
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    text: "Map Warning"
                    color: animusTheme.text
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: root.statusText()
                    color: animusTheme.mutedText
                    elide: Text.ElideRight
                }
            }

            AnimusIconButton {
                text: root.mapWarningExpanded ? "\u25b2" : "\u25bc"
                toolTipText: root.mapWarningExpanded ? "Hide map warning details"
                                                       : "Show map warning details"
                Layout.preferredWidth: 30
                Layout.preferredHeight: 28
                onClicked: root.mapWarningExpanded = !root.mapWarningExpanded
            }

            AnimusIconButton {
                text: "\u00d7"
                toolTipText: "Dismiss map warning"
                Layout.preferredWidth: 30
                Layout.preferredHeight: 28
                onClicked: root.mapWarningDismissed = true
            }
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: warningHeader.bottom
            anchors.leftMargin: 42
            anchors.rightMargin: 14
            anchors.topMargin: 8
            spacing: 7
            visible: root.mapWarningExpanded
            z: 1

            Label {
                Layout.fillWidth: true
                text: root.providerBlockMessage
                color: animusTheme.text
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: "Cache DB: " + (mapCache.cacheDatabasePath || "none")
                color: animusTheme.mutedText
                elide: Text.ElideRight
            }
        }
    }

    AnimusOverlayPanel {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 12
        padding: 6
        panelOpacity: 0.82

        GridLayout {
            columns: 3
            rowSpacing: 2
            columnSpacing: 2
            AnimusIconButton {
                text: "\u25b2"
                toolTipText: "Pan north"
                Layout.column: 1
                Layout.preferredWidth: 34
                Layout.preferredHeight: 30
                onClicked: root.panByPixels(0, 96)
            }
            AnimusIconButton {
                text: "\u25c0"
                toolTipText: "Pan west"
                Layout.row: 1
                Layout.column: 0
                Layout.preferredWidth: 34
                Layout.preferredHeight: 30
                onClicked: root.panByPixels(96, 0)
            }
            AnimusIconButton {
                text: "\u21ba"
                toolTipText: "Recenter on vehicle"
                Layout.row: 1
                Layout.column: 1
                Layout.preferredWidth: 34
                Layout.preferredHeight: 30
                onClicked: root.recenterOnVehicle()
            }
            AnimusIconButton {
                text: "\u25b6"
                toolTipText: "Pan east"
                Layout.row: 1
                Layout.column: 2
                Layout.preferredWidth: 34
                Layout.preferredHeight: 30
                onClicked: root.panByPixels(-96, 0)
            }
            AnimusIconButton {
                text: "\u25bc"
                toolTipText: "Pan south"
                Layout.row: 2
                Layout.column: 1
                Layout.preferredWidth: 34
                Layout.preferredHeight: 30
                onClicked: root.panByPixels(0, -96)
            }
        }
    }

    Label {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: Math.min(parent.width - 20, implicitWidth)
        text: root.attributionText()
        color: animusTheme.text
        padding: 6
        elide: Text.ElideRight
        background: Rectangle { color: animusTheme.overlay; opacity: 0.9; radius: 4 }
    }
}
