import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "terrain3DView"

    property bool lastCaptureOk: false
    property string lastCaptureError: ""
    property bool lastControlSurfaceInspectionOk: false
    property string lastControlSurfaceInspectionError: ""
    property string cameraMode: "chase"
    property var localSceneStatus: ({ "status": "initializing", "error": "" })
    property bool workspaceCurrent: StackLayout.isCurrentItem

    signal captureFinished(bool ok, string error)
    signal controlSurfaceInspectionFinished(bool ok, string error)

    function captureCesiumPng(path) {
        if (!webLoader.active || webLoader.status !== Loader.Ready || !webLoader.item) {
            root.lastCaptureOk = false
            root.lastCaptureError = "Terrain WebEngine view is not ready"
            root.captureFinished(false, "Terrain WebEngine view is not ready")
            return
        }
        webLoader.item.captureCesiumPng(path)
    }

    function inspectControlSurfaces(path, snapshot) {
        if (!webLoader.active || webLoader.status !== Loader.Ready || !webLoader.item) {
            root.lastControlSurfaceInspectionOk = false
            root.lastControlSurfaceInspectionError = "Terrain WebEngine view is not ready"
            root.controlSurfaceInspectionFinished(false, "Terrain WebEngine view is not ready")
            return
        }
        webLoader.item.inspectControlSurfaces(path, snapshot)
    }

    function setCameraMode(mode) {
        root.cameraMode = mode
        if (webLoader.active && webLoader.status === Loader.Ready && webLoader.item)
            webLoader.item.setCameraMode(mode)
    }

    function statusText() {
        var terrain = cesiumBridge.terrainStatus.provider === "quantized-mesh"
                ? "terrain available: " + cesiumBridge.terrainStatus.cachePath
                : "terrain fixture: " + cesiumBridge.terrainStatus.fixture.name
        var scene = root.localSceneStatus.status || "initializing"
        if (root.localSceneStatus.error)
            return terrain + " | " + scene + ": " + root.localSceneStatus.error
        return terrain + " | " + scene
    }

    function clearanceText(value, suffix) {
        if (cesiumBridge.terrainClearance.state === "unknown")
            return "--"
        return value.toFixed(1) + suffix
    }

    function clearanceColor() {
        if (cesiumBridge.terrainClearance.state === "warning")
            return animusTheme.danger
        if (cesiumBridge.terrainClearance.state === "caution")
            return animusTheme.warning
        if (cesiumBridge.terrainClearance.state === "clear")
            return animusTheme.success
        return animusTheme.mutedText
    }

    function useFallbackScene() {
        var status = root.localSceneStatus.status || "initializing"
        return !webLoader.active || webLoader.status === Loader.Error ||
               status === "initializing" || status.indexOf("error") >= 0
    }

    Rectangle {
        anchors.fill: parent
        color: animusTheme.mapBackground
    }

    Canvas {
        id: fallbackCanvas
        anchors.fill: parent
        visible: root.useFallbackScene()
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var sky = ctx.createLinearGradient(0, 0, 0, height * 0.58)
            sky.addColorStop(0, animusTheme.sceneSkyTop)
            sky.addColorStop(1, animusTheme.sceneSkyBottom)
            ctx.fillStyle = sky
            ctx.fillRect(0, 0, width, height)

            function ridge(points, fill, stroke) {
                ctx.beginPath()
                ctx.moveTo(0, height)
                for (var i = 0; i < points.length; ++i)
                    ctx.lineTo(points[i][0] * width, points[i][1] * height)
                ctx.lineTo(width, height)
                ctx.closePath()
                ctx.fillStyle = fill
                ctx.fill()
                ctx.strokeStyle = stroke
                ctx.lineWidth = 2
                ctx.stroke()
            }

            ridge([[0, 0.64], [0.12, 0.52], [0.24, 0.59], [0.38, 0.43], [0.55, 0.55], [0.72, 0.46], [1, 0.61]],
                  animusTheme.sceneRidge, animusTheme.border)
            ridge([[0, 0.78], [0.16, 0.69], [0.32, 0.73], [0.48, 0.62], [0.68, 0.7], [0.82, 0.59], [1, 0.73]],
                  animusTheme.sceneRidgeDark, animusTheme.border)

            ctx.strokeStyle = animusTheme.sceneGroundLine
            ctx.lineWidth = 1
            for (var y = 0.68; y < 0.96; y += 0.055) {
                ctx.beginPath()
                ctx.moveTo(width * 0.05, height * y)
                ctx.bezierCurveTo(width * 0.32, height * (y - 0.03),
                                  width * 0.61, height * (y + 0.04),
                                  width * 0.95, height * (y - 0.015))
                ctx.stroke()
            }
        }
    }
    Connections {
        target: animusTheme
        function onThemeChanged() { fallbackCanvas.requestPaint() }
    }

    Rectangle {
        width: 46
        height: 46
        radius: 23
        x: parent.width * 0.5 - width / 2
        y: parent.height * 0.42 - height / 2
        visible: fallbackCanvas.visible
        color: animusTheme.accent
        border.color: animusTheme.surface
        border.width: 3

        Rectangle {
            width: 4
            height: 70
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            color: animusTheme.accent
            opacity: 0.45
        }
    }

    Loader {
        id: webLoader
        anchors.fill: parent
        active: webEngineTerrainEnabled && root.workspaceCurrent
        visible: !root.useFallbackScene()
        source: "Terrain3DWebView.qml"
        onLoaded: {
            item.workspaceMode = "terrain-3d"
            item.setCameraMode(root.cameraMode)
        }
    }

    Connections {
        target: webLoader.item
        ignoreUnknownSignals: true
        function onCaptureFinished(ok, error) {
            root.lastCaptureOk = ok
            root.lastCaptureError = error
            root.captureFinished(ok, error)
        }
        function onControlSurfaceInspectionFinished(ok, error) {
            root.lastControlSurfaceInspectionOk = ok
            root.lastControlSurfaceInspectionError = error
            root.controlSurfaceInspectionFinished(ok, error)
        }
        function onCameraModeChanged(mode) {
            if (mode === "chase" || mode === "orbit" || mode === "free")
                root.cameraMode = mode
        }
        function onSceneStatusChanged() {
            root.localSceneStatus = webLoader.item.sceneStatus
        }
    }

    AnimusOverlayPanel {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        Label {
            text: root.statusText()
            color: animusTheme.text
        }
    }

    TelemetryStrip {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
    }

    AnimusOverlayPanel {
        objectName: "terrainClearanceOverlay"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: Math.min(parent.width - 24, 370)
        borderColor: root.clearanceColor()
        borderWidth: 2

        GridLayout {
            columns: 2
            anchors.fill: parent
            rowSpacing: 8
            columnSpacing: 8

            Label {
                text: "Clearance"
                font.bold: true
                color: root.clearanceColor()
            }
            AnimusStatusBadge {
                text: cesiumBridge.terrainClearance.state || "unknown"
                tone: cesiumBridge.terrainClearance.state === "warning" ? "danger"
                      : cesiumBridge.terrainClearance.state === "caution" ? "warning"
                      : cesiumBridge.terrainClearance.state === "clear" ? "success"
                      : "neutral"
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 110
            }
            Label { text: "AGL"; color: animusTheme.mutedText }
            Label {
                text: root.clearanceText(cesiumBridge.terrainClearance.aglM, " m")
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 110
            }
            Label { text: "Min recent"; color: animusTheme.mutedText }
            Label {
                text: root.clearanceText(cesiumBridge.terrainClearance.minimumRecentClearanceM,
                                         " m")
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 110
            }
            Label { text: "Trend"; color: animusTheme.mutedText }
            Label {
                text: root.clearanceText(cesiumBridge.terrainClearance.trendMps, " m/s")
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 110
            }
            Label {
                text: cesiumBridge.terrainClearance.message || "terrain clearance unavailable"
                color: animusTheme.mutedText
                Layout.columnSpan: 2
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }

    AnimusSegmentedControl {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        segments: [
            { "label": "Chase", "value": "chase" },
            { "label": "Orbit", "value": "orbit" },
            { "label": "Free", "value": "free" }
        ]
        currentValue: root.cameraMode
        onSelected: function(value) { root.setCameraMode(value) }
    }
}
