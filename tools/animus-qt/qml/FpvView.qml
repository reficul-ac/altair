import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "fpvView"

    property bool lastCaptureOk: false
    property string lastCaptureError: ""
    property bool lastControlSurfaceInspectionOk: false
    property string lastControlSurfaceInspectionError: ""
    property bool lastCameraInspectionOk: false
    property string lastCameraInspectionError: ""
    property var localSceneStatus: ({ "status": "initializing", "error": "" })
    property bool workspaceCurrent: StackLayout.isCurrentItem

    signal captureFinished(bool ok, string error)
    signal controlSurfaceInspectionFinished(bool ok, string error)
    signal cameraInspectionFinished(bool ok, string error)

    function writeFallbackDiagnostic(path, extra) {
        var diagnostic = extra || {}
        diagnostic.ok = false
        diagnostic.renderer = "qml-fallback"
        diagnostic.workspaceMode = "fpv"
        diagnostic.failures = diagnostic.failures || ["FPV WebEngine view is not ready"]
        captureWriter.writeTextFile(path, JSON.stringify(diagnostic, null, 2) + "\n")
    }

    function captureCesiumPng(path) {
        if (!webLoader.active || webLoader.status !== Loader.Ready || !webLoader.item) {
            root.lastCaptureOk = false
            root.lastCaptureError = "FPV WebEngine view is not ready"
            root.captureFinished(false, root.lastCaptureError)
            return
        }
        webLoader.item.captureCesiumPng(path)
    }

    function inspectControlSurfaces(path, snapshot) {
        if (!webLoader.active || webLoader.status !== Loader.Ready || !webLoader.item) {
            root.lastControlSurfaceInspectionOk = false
            root.lastControlSurfaceInspectionError = "FPV WebEngine view is not ready"
            root.writeFallbackDiagnostic(path, {
                profileLoaded: false,
                modelLoaded: false,
                surfaceCount: 0,
                surfaces: []
            })
            root.controlSurfaceInspectionFinished(false, root.lastControlSurfaceInspectionError)
            return
        }
        webLoader.item.inspectControlSurfaces(path, snapshot)
    }

    function inspectCameraState(path) {
        if (!webLoader.active || webLoader.status !== Loader.Ready || !webLoader.item) {
            root.lastCameraInspectionOk = false
            root.lastCameraInspectionError = "FPV WebEngine view is not ready"
            root.writeFallbackDiagnostic(path, {
                mode: "fallback",
                cameraMode: "fallback",
                freeRoamAvailable: false,
                vehicleLocked: false,
                fixedFovDeg: 70.0,
                ownshipHidden: true,
                terrainEnabled: true
            })
            root.cameraInspectionFinished(false, root.lastCameraInspectionError)
            return
        }
        webLoader.item.inspectCameraState(path)
    }

    function resetCamera() {
        if (webLoader.active && webLoader.status === Loader.Ready && webLoader.item)
            webLoader.item.resetFpvCamera()
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
            ctx.fillStyle = animusTheme.sceneRidge
            ctx.beginPath()
            ctx.moveTo(0, height)
            ctx.lineTo(width * 0.18, height * 0.62)
            ctx.lineTo(width * 0.38, height * 0.46)
            ctx.lineTo(width * 0.62, height * 0.59)
            ctx.lineTo(width * 0.82, height * 0.48)
            ctx.lineTo(width, height * 0.64)
            ctx.lineTo(width, height)
            ctx.closePath()
            ctx.fill()
            ctx.fillStyle = animusTheme.sceneRidgeDark
            ctx.beginPath()
            ctx.moveTo(0, height)
            ctx.lineTo(width * 0.16, height * 0.74)
            ctx.lineTo(width * 0.42, height * 0.66)
            ctx.lineTo(width * 0.68, height * 0.76)
            ctx.lineTo(width * 0.90, height * 0.62)
            ctx.lineTo(width, height * 0.74)
            ctx.lineTo(width, height)
            ctx.closePath()
            ctx.fill()
        }
    }
    Connections {
        target: animusTheme
        function onThemeChanged() { fallbackCanvas.requestPaint() }
    }

    Loader {
        id: webLoader
        anchors.fill: parent
        active: webEngineTerrainEnabled && root.workspaceCurrent
        visible: !root.useFallbackScene()
        source: "Terrain3DWebView.qml"
        onLoaded: {
            item.workspaceMode = "fpv"
            item.resetFpvCamera()
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
        function onCameraInspectionFinished(ok, error) {
            root.lastCameraInspectionOk = ok
            root.lastCameraInspectionError = error
            root.cameraInspectionFinished(ok, error)
        }
        function onSceneStatusChanged() {
            root.localSceneStatus = webLoader.item.sceneStatus
        }
    }

    AnimusSceneStatus {
        objectName: "fpvSceneStatusOverlay"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        workspaceLabel: "FPV"
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

    Canvas {
        id: fpvFlightPathCue
        width: 360
        height: 250
        anchors.centerIn: parent
        opacity: vehicleModel.attitudeValid && telemetryService.linkFresh ? 0.78 : 0.38
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var cx = width * 0.5
            var cy = height * 0.5

            ctx.strokeStyle = animusTheme.surface
            ctx.lineWidth = 5
            ctx.beginPath()
            ctx.arc(cx, cy, 92, Math.PI * 1.08, Math.PI * 1.92)
            ctx.stroke()

            ctx.strokeStyle = vehicleModel.attitudeValid && telemetryService.linkFresh
                              ? animusTheme.accent : animusTheme.warning
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.arc(cx, cy, 92, Math.PI * 1.08, Math.PI * 1.92)
            ctx.stroke()

            ctx.strokeStyle = animusTheme.sceneGroundLine
            ctx.lineWidth = 1
            for (var i = 0; i < 4; ++i) {
                var offset = 32 + i * 30
                ctx.beginPath()
                ctx.moveTo(cx - 128 + i * 12, cy + offset)
                ctx.lineTo(cx - 38, cy + offset)
                ctx.moveTo(cx + 38, cy + offset)
                ctx.lineTo(cx + 128 - i * 12, cy + offset)
                ctx.stroke()
            }

            ctx.strokeStyle = animusTheme.success
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(cx - 22, cy - 34)
            ctx.lineTo(cx, cy - 50)
            ctx.lineTo(cx + 22, cy - 34)
            ctx.stroke()
        }
    }

    FpvAttitudeCue {
        objectName: "fpvAttitudeCue"
        anchors.centerIn: parent
        rollRad: vehicleModel.rollRad
        pitchRad: vehicleModel.pitchRad
        valid: vehicleModel.attitudeValid && telemetryService.linkFresh
    }

    AnimusSceneCueOverlay {
        objectName: "fpvSceneCueOverlay"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        sceneMode: "fpv"
        clearance: cesiumBridge.terrainClearance
        cameraState: "FPV lock"
        cameraLocked: true
        resetAvailable: webLoader.active && webLoader.status === Loader.Ready && webLoader.item !== null
    }

    AnimusIconButton {
        objectName: "fpvSnapButton"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        text: "\u21ba"
        toolTipText: "Reset FPV camera"
        onClicked: root.resetCamera()
    }

    Connections {
        target: animusTheme
        function onThemeChanged() { fpvFlightPathCue.requestPaint() }
    }
    Connections {
        target: vehicleModel
        function onAttitudeChanged() { fpvFlightPathCue.requestPaint() }
    }
    Connections {
        target: telemetryService
        function onFreshnessChanged() { fpvFlightPathCue.requestPaint() }
    }
}
