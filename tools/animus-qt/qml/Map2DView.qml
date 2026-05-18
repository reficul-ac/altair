import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property bool following: true
    property bool mapWarningExpanded: false
    property bool mapWarningDismissed: false
    property double manualCenterLatitudeDeg: vehicleModel.latitudeDeg
    property double manualCenterLongitudeDeg: vehicleModel.longitudeDeg
    property int zoomLevel: 15

    readonly property bool providerAllowed:
        mapCache.providerBlockReason(mapCache.activeProviderId, offlineMaps.networkAllowed) === ""
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
        var blockReason = mapCache.providerBlockReason(mapCache.activeProviderId,
                                                       offlineMaps.networkAllowed)
        if (blockReason)
            return blockReason
        return mapCache.activeProviderId + " / " + mapCache.activeMapTypeId +
               " | " + root.defaultTileSetStatus()
    }

    function attributionText() {
        return mapCache.activeAttribution + " | QGC-style cache | " + mapCache.activeMapTypeId
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

    Rectangle {
        anchors.fill: parent
        color: "#dfe7de"

        Repeater {
            model: 16
            Rectangle {
                width: parent.width
                height: 1
                y: index * parent.height / 15
                color: "#c6d0c5"
            }
        }

        Repeater {
            model: 24
            Rectangle {
                width: 1
                height: parent.height
                x: index * parent.width / 23
                color: "#c6d0c5"
            }
        }

        Rectangle {
            x: parent.width * 0.08
            y: parent.height * 0.58
            width: parent.width * 0.84
            height: 28
            radius: 14
            rotation: -11
            color: "#b9c7bd"
            opacity: 0.85
        }

        Rectangle {
            x: parent.width * 0.2
            y: parent.height * 0.22
            width: parent.width * 0.44
            height: 22
            radius: 11
            rotation: 31
            color: "#ccd7cb"
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

    Repeater {
        model: breadcrumbModel
        delegate: Rectangle {
            width: 7
            height: 7
            radius: 3.5
            x: root.projectX(longitude) - width / 2
            y: root.projectY(latitude) - height / 2
            color: "#1d6fd6"
            opacity: 0.42
        }
    }

    Rectangle {
        width: 24
        height: 24
        radius: 12
        x: root.projectX(vehicleModel.homeLongitudeDeg) - width / 2
        y: root.projectY(vehicleModel.homeLatitudeDeg) - height / 2
        color: "#f7f7f3"
        border.color: "#b32020"
        border.width: 2

        Label {
            anchors.centerIn: parent
            text: "H"
            color: "#b32020"
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
                ctx.fillStyle = "#1d6fd6"
                ctx.strokeStyle = "white"
                ctx.lineWidth = 3
                ctx.fill()
                ctx.stroke()
            }
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

    Frame {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        width: Math.min(parent.width - 24, 660)
        background: Rectangle { color: "#f7f7f3"; border.color: "#c9c9c0"; radius: 6 }
        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: offlineMaps.modeLabel()
                    font.bold: true
                }
                ComboBox {
                    id: providerSelector
                    Layout.preferredWidth: 220
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
                    color: root.providerAllowed ? "#4b5563" : "#7a4b00"
                }
            }

            RowLayout {
                Button {
                    text: "-"
                    Layout.preferredWidth: 36
                    enabled: root.zoomLevel > root.clampZoom(root.zoomLevel - 1)
                    onClicked: root.zoomBy(-1)
                }
                Label {
                    text: "z" + root.zoomLevel
                    horizontalAlignment: Text.AlignHCenter
                    Layout.preferredWidth: 42
                }
                Button {
                    text: "+"
                    Layout.preferredWidth: 36
                    enabled: root.zoomLevel < root.clampZoom(root.zoomLevel + 1)
                    onClicked: root.zoomBy(1)
                }
                Button {
                    text: "Snap"
                    onClicked: root.recenterOnVehicle()
                }
                Label {
                    text: root.following ? "Following vehicle" : "Manual pan"
                    color: root.following ? "#0f7b43" : "#7a4b00"
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.scaleLabel()
                    color: "#4b5563"
                }
                Rectangle {
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 6
                    color: "#202020"
                }
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
        color: "#fbfbf8"
        border.color: "#d59b28"
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
                color: "#f3b334"
                Label {
                    anchors.centerIn: parent
                    text: "!"
                    color: "#1f2933"
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    text: "Map Warning"
                    color: "#1f2933"
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: root.statusText()
                    color: "#4b5563"
                    elide: Text.ElideRight
                }
            }

            Button {
                text: root.mapWarningExpanded ? "^" : "v"
                Layout.preferredWidth: 30
                Layout.preferredHeight: 28
                onClicked: root.mapWarningExpanded = !root.mapWarningExpanded
            }

            Button {
                text: "x"
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
                text: mapCache.providerBlockReason(mapCache.activeProviderId,
                                                   offlineMaps.networkAllowed)
                color: "#3f4a3d"
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: "Cache DB: " + (mapCache.cacheDatabasePath || "none")
                color: "#4b5563"
                elide: Text.ElideRight
            }
        }
    }

    Frame {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 12
        background: Rectangle { color: "#f7f7f3"; border.color: "#c9c9c0"; radius: 6 }
        GridLayout {
            columns: 3
            rowSpacing: 2
            columnSpacing: 2
            Button {
                text: "^"
                Layout.column: 1
                Layout.preferredWidth: 34
                Layout.preferredHeight: 30
                onClicked: root.panByPixels(0, 96)
            }
            Button {
                text: "<"
                Layout.row: 1
                Layout.column: 0
                Layout.preferredWidth: 34
                Layout.preferredHeight: 30
                onClicked: root.panByPixels(96, 0)
            }
            Button {
                text: "o"
                Layout.row: 1
                Layout.column: 1
                Layout.preferredWidth: 34
                Layout.preferredHeight: 30
                onClicked: root.recenterOnVehicle()
            }
            Button {
                text: ">"
                Layout.row: 1
                Layout.column: 2
                Layout.preferredWidth: 34
                Layout.preferredHeight: 30
                onClicked: root.panByPixels(-96, 0)
            }
            Button {
                text: "v"
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
        color: "#202020"
        padding: 6
        elide: Text.ElideRight
        background: Rectangle { color: "#f7f7f3"; opacity: 0.9; radius: 4 }
    }
}
