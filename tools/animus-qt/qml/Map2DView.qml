import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property bool following: true
    property double manualCenterLatitudeDeg: vehicleModel.latitudeDeg
    property double manualCenterLongitudeDeg: vehicleModel.longitudeDeg
    property int zoomLevel: defaultZoom()

    readonly property bool sourceAllowed: offlineMaps.canUseSource(mapSources.activeSourceId)
    readonly property bool sourceIsOfflinePack: mapSources.activeSourceId === "offline-pack"
    readonly property bool hasRasterTiles: sourceIsOfflinePack && sourceAllowed &&
                                           mapPacks.activeHasLocalXyzImagery
    readonly property double centerLatitudeDeg: following ? vehicleModel.latitudeDeg
                                                          : manualCenterLatitudeDeg
    readonly property double centerLongitudeDeg: following ? vehicleModel.longitudeDeg
                                                           : manualCenterLongitudeDeg
    readonly property int tileSize: 256
    readonly property double degreesPerPixel: hasRasterTiles
                                             ? 360.0 / (tileSize * Math.pow(2, zoomLevel))
                                             : 0.000014 * Math.pow(2, 15 - zoomLevel)
    readonly property int tileSpan: Math.pow(2, zoomLevel)
    readonly property double centerPixelX: lonToPixelX(centerLongitudeDeg, zoomLevel)
    readonly property double centerPixelY: latToPixelY(centerLatitudeDeg, zoomLevel)
    readonly property int firstTileX: Math.floor((centerPixelX - width / 2) / tileSize)
    readonly property int firstTileY: Math.floor((centerPixelY - height / 2) / tileSize)
    readonly property int tileColumns: hasRasterTiles ? Math.ceil(width / tileSize) + 2 : 0
    readonly property int tileRows: hasRasterTiles ? Math.ceil(height / tileSize) + 2 : 0

    Connections {
        target: mapPacks
        function onActivePackChanged() {
            root.zoomLevel = root.defaultZoom()
        }
    }

    Connections {
        target: offlineMaps
        function onModeChanged() {
            if (!offlineMaps.canUseSource(mapSources.activeSourceId))
                mapSources.activeSourceId = "offline-pack"
        }
    }

    Connections {
        target: vehicleModel
        function onPositionChanged() {
            if (root.following)
                root.syncManualCenterToVehicle()
        }
    }

    function defaultZoom() {
        if (mapPacks.activeHasLocalXyzImagery)
            return Math.max(mapPacks.activeMinZoom, Math.min(mapPacks.activeMaxZoom, 15))
        return 15
    }

    function clampLatitude(latitudeDeg) {
        return Math.max(-85.05112878, Math.min(85.05112878, latitudeDeg))
    }

    function clampZoom(zoom) {
        if (mapPacks.activeHasLocalXyzImagery)
            return Math.max(mapPacks.activeMinZoom, Math.min(mapPacks.activeMaxZoom, zoom))
        return Math.max(3, Math.min(20, zoom))
    }

    function lonToPixelX(longitudeDeg, zoom) {
        return (longitudeDeg + 180.0) / 360.0 * tileSize * Math.pow(2, zoom)
    }

    function latToPixelY(latitudeDeg, zoom) {
        var latRad = clampLatitude(latitudeDeg) * Math.PI / 180.0
        return (1.0 - Math.log(Math.tan(latRad) + 1.0 / Math.cos(latRad)) / Math.PI) /
                2.0 * tileSize * Math.pow(2, zoom)
    }

    function pixelXToLon(pixelX, zoom) {
        return pixelX / (tileSize * Math.pow(2, zoom)) * 360.0 - 180.0
    }

    function pixelYToLat(pixelY, zoom) {
        var n = Math.PI - 2.0 * Math.PI * pixelY / (tileSize * Math.pow(2, zoom))
        return 180.0 / Math.PI * Math.atan(0.5 * (Math.exp(n) - Math.exp(-n)))
    }

    function wrappedLongitude(longitudeDeg) {
        var wrapped = ((longitudeDeg + 180.0) % 360.0 + 360.0) % 360.0 - 180.0
        return wrapped === -180.0 ? 180.0 : wrapped
    }

    function wrappedTileX(tileX) {
        return ((tileX % tileSpan) + tileSpan) % tileSpan
    }

    function clampedTileY(tileY) {
        return Math.max(0, Math.min(tileSpan - 1, tileY))
    }

    function projectX(longitudeDeg) {
        if (hasRasterTiles)
            return width / 2 + lonToPixelX(longitudeDeg, zoomLevel) - centerPixelX
        return width / 2 + (longitudeDeg - centerLongitudeDeg) / degreesPerPixel
    }

    function projectY(latitudeDeg) {
        if (hasRasterTiles)
            return height / 2 + latToPixelY(latitudeDeg, zoomLevel) - centerPixelY
        return height / 2 - (latitudeDeg - centerLatitudeDeg) / degreesPerPixel
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
        if (hasRasterTiles) {
            manualCenterLongitudeDeg = wrappedLongitude(pixelXToLon(centerPixelX - deltaX, zoomLevel))
            manualCenterLatitudeDeg = clampLatitude(pixelYToLat(centerPixelY - deltaY, zoomLevel))
        } else {
            manualCenterLongitudeDeg = wrappedLongitude(manualCenterLongitudeDeg -
                                                        deltaX * degreesPerPixel)
            manualCenterLatitudeDeg = clampLatitude(manualCenterLatitudeDeg +
                                                    deltaY * degreesPerPixel)
        }
    }

    function zoomBy(delta) {
        var nextZoom = clampZoom(zoomLevel + delta)
        if (nextZoom === zoomLevel)
            return
        syncManualCenterToCurrentView()
        zoomLevel = nextZoom
    }

    function syncManualCenterToCurrentView() {
        manualCenterLatitudeDeg = centerLatitudeDeg
        manualCenterLongitudeDeg = centerLongitudeDeg
        following = false
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
        if (!sourceAllowed)
            return offlineMaps.sourceBlockReason(mapSources.activeSourceId)
        if (!sourceIsOfflinePack)
            return "Provider renderer pending for " + mapSources.activeLabel()
        if (!mapPacks.activePackId)
            return "No offline map pack selected"
        if (!mapPacks.activeHasLocalXyzImagery)
            return "Active pack has no local XYZ imagery"
        return mapSources.activeLabel() + ": " + mapPacks.activePackId + " z" + zoomLevel
    }

    Rectangle {
        anchors.fill: parent
        visible: !root.hasRasterTiles
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

    Item {
        anchors.fill: parent
        visible: root.hasRasterTiles
        clip: true

        Repeater {
            model: root.tileColumns * root.tileRows
            delegate: Image {
                readonly property int column: index % root.tileColumns
                readonly property int row: Math.floor(index / root.tileColumns)
                readonly property int tileX: root.firstTileX + column
                readonly property int tileY: root.firstTileY + row
                readonly property int sourceX: root.wrappedTileX(tileX)
                readonly property int sourceY: root.clampedTileY(tileY)

                x: tileX * root.tileSize - (root.centerPixelX - root.width / 2)
                y: tileY * root.tileSize - (root.centerPixelY - root.height / 2)
                width: root.tileSize
                height: root.tileSize
                sourceSize.width: root.tileSize
                sourceSize.height: root.tileSize
                fillMode: Image.Stretch
                asynchronous: true
                cache: true
                source: "image://animusTiles/" + encodeURIComponent(mapPacks.activePackId) +
                        "/" + root.zoomLevel + "/" + sourceX + "/" + sourceY
            }
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
        id: homeMarker
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
        id: vehicleMarker
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
        id: mapMouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        property real lastX: 0
        property real lastY: 0

        onPressed: function(mouse) {
            lastX = mouse.x
            lastY = mouse.y
            root.syncManualCenterToCurrentView()
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
        width: Math.min(parent.width - 24, 620)
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
                    id: sourceSelector
                    Layout.preferredWidth: 210
                    model: mapSources
                    textRole: "label"
                    currentIndex: mapSources.sourceIndex(mapSources.activeSourceId)
                    delegate: ItemDelegate {
                        width: sourceSelector.width
                        enabled: offlineMaps.canUseSource(sourceId)
                        text: label + (networkRequired ? " (network)" : "")
                    }
                    onActivated: function(index) {
                        var selectedId = mapSources.sourceIdAt(index)
                        if (offlineMaps.canUseSource(selectedId))
                            mapSources.activeSourceId = selectedId
                    }
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: root.statusText()
                    color: root.sourceAllowed && root.sourceIsOfflinePack ? "#4b5563" : "#7a4b00"
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
        text: (root.sourceIsOfflinePack ? (mapPacks.activeAttribution() ||
                                           mapSources.activeAttribution())
                                         : mapSources.activeAttribution()) +
              (root.hasRasterTiles ? " | Local XYZ" : " | QtQuick fallback")
        color: "#202020"
        padding: 6
        elide: Text.ElideRight
        background: Rectangle { color: "#f7f7f3"; opacity: 0.9; radius: 4 }
    }
}
