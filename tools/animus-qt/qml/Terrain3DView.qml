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

    function useFallbackScene() {
        var status = root.localSceneStatus.status || "initializing"
        return !webLoader.active || webLoader.status === Loader.Error ||
               status === "initializing" || status.indexOf("error") >= 0
    }

    function clearanceState() {
        return cesiumBridge.terrainClearance && cesiumBridge.terrainClearance.state
               ? cesiumBridge.terrainClearance.state : "unknown"
    }

    function clearanceColor() {
        var state = root.clearanceState()
        if (state === "warning")
            return animusTheme.danger
        if (state === "caution")
            return animusTheme.warning
        if (state === "clear")
            return animusTheme.success
        return animusTheme.border
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

    AnimusSceneStatus {
        objectName: "terrainSceneStatusOverlay"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        workspaceLabel: "Terrain 3D"
        sceneStatus: root.localSceneStatus
        terrainStatus: cesiumBridge.terrainStatus
        webSceneActive: webLoader.active
        webSceneReady: webLoader.status === Loader.Ready && webLoader.item !== null
        webSceneError: webLoader.status === Loader.Error
        fallbackActive: root.useFallbackScene()
    }

    AnimusTelemetrySummary {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
    }

    AnimusSceneCueOverlay {
        objectName: "terrainClearanceOverlay"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        sceneMode: "terrain"
        clearance: cesiumBridge.terrainClearance
        cameraState: root.cameraMode === "free" ? "camera free" : root.cameraMode + " lock"
        cameraLocked: root.cameraMode !== "free"
        resetAvailable: false
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
