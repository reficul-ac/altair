import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "tacticalAttitudeView"

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
        diagnostic.workspaceMode = "tactical"
        diagnostic.failures = diagnostic.failures || ["Tactical WebEngine view is not ready"]
        captureWriter.writeTextFile(path, JSON.stringify(diagnostic, null, 2) + "\n")
    }

    function captureCesiumPng(path) {
        if (!webLoader.active || webLoader.status !== Loader.Ready || !webLoader.item) {
            root.lastCaptureOk = false
            root.lastCaptureError = "Tactical WebEngine view is not ready"
            root.captureFinished(false, root.lastCaptureError)
            return
        }
        webLoader.item.captureCesiumPng(path)
    }

    function inspectControlSurfaces(path, snapshot) {
        if (!webLoader.active || webLoader.status !== Loader.Ready || !webLoader.item) {
            root.lastControlSurfaceInspectionOk = false
            root.lastControlSurfaceInspectionError = "Tactical WebEngine view is not ready"
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
            root.lastCameraInspectionError = "Tactical WebEngine view is not ready"
            root.writeFallbackDiagnostic(path, {
                mode: "fallback",
                cameraMode: "fallback",
                freeRoamAvailable: false,
                vehicleLocked: false
            })
            root.cameraInspectionFinished(false, root.lastCameraInspectionError)
            return
        }
        webLoader.item.inspectCameraState(path)
    }

    function resetCamera() {
        if (webLoader.active && webLoader.status === Loader.Ready && webLoader.item)
            webLoader.item.resetTacticalCamera()
    }

    function valueText(valid, value, suffix, digits) {
        if (!telemetryService.linkFresh)
            return "STALE"
        if (!valid)
            return "UNK"
        return Number(value).toFixed(digits) + suffix
    }

    function useFallbackScene() {
        var status = root.localSceneStatus.status || "initializing"
        return !webLoader.active || webLoader.status === Loader.Error ||
               status === "initializing" || status.indexOf("error") >= 0
    }

    Rectangle {
        anchors.fill: parent
        color: animusTheme.tacticalBackground
    }

    Loader {
        id: webLoader
        anchors.fill: parent
        active: webEngineTerrainEnabled && root.workspaceCurrent
        visible: !root.useFallbackScene()
        source: "Terrain3DWebView.qml"
        onLoaded: {
            item.workspaceMode = "tactical"
            item.resetTacticalCamera()
        }
    }

    Canvas {
        id: fallbackCanvas
        anchors.fill: parent
        visible: root.useFallbackScene()
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = animusTheme.tacticalBackground
            ctx.fillRect(0, 0, width, height)
            var colors = [animusTheme.danger, animusTheme.success, animusTheme.accent]
            ctx.lineWidth = 3
            for (var i = 0; i < colors.length; ++i) {
                ctx.strokeStyle = colors[i]
                ctx.beginPath()
                ctx.arc(width * 0.5, height * 0.52, (i + 2) * Math.min(width, height) * 0.055, 0, Math.PI * 2)
                ctx.stroke()
            }
            ctx.save()
            ctx.translate(width * 0.5, height * 0.52)
            ctx.rotate(vehicleModel.rollRad)

            ctx.fillStyle = animusTheme.surface
            ctx.strokeStyle = animusTheme.text
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(66, 0)
            ctx.lineTo(10, -10)
            ctx.lineTo(-58, -8)
            ctx.lineTo(-72, 0)
            ctx.lineTo(-58, 8)
            ctx.lineTo(10, 10)
            ctx.closePath()
            ctx.fill()
            ctx.stroke()

            ctx.fillStyle = animusTheme.accent
            ctx.beginPath()
            ctx.ellipse(22, -1, 11, 5, 0, 0, Math.PI * 2)
            ctx.fill()

            ctx.fillStyle = animusTheme.mutedText
            ctx.beginPath()
            ctx.moveTo(-5, -7)
            ctx.lineTo(-18, -56)
            ctx.lineTo(-30, -53)
            ctx.lineTo(-22, -6)
            ctx.closePath()
            ctx.fill()
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(-5, 7)
            ctx.lineTo(-18, 56)
            ctx.lineTo(-30, 53)
            ctx.lineTo(-22, 6)
            ctx.closePath()
            ctx.fill()
            ctx.stroke()

            ctx.fillStyle = animusTheme.border
            ctx.fillRect(-65, -23, 24, 7)
            ctx.fillRect(-65, 16, 24, 7)
            ctx.fillRect(-72, -5, 18, 10)
            ctx.restore()
        }
    }
    Connections {
        target: animusTheme
        function onThemeChanged() { fallbackCanvas.requestPaint() }
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

    AnimusOverlayPanel {
        objectName: "tacticalAttitudeOverlay"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        padding: 10
        borderColor: telemetryService.linkFresh ? animusTheme.success : animusTheme.warning

        GridLayout {
            columns: 2
            rowSpacing: 5
            columnSpacing: 12

            Label { text: "Roll"; color: animusTheme.mutedText; font.bold: true }
            Label {
                text: root.valueText(vehicleModel.attitudeValid,
                                     vehicleModel.rollRad * 180.0 / Math.PI, " deg", 1)
            }
            Label { text: "Pitch"; color: animusTheme.mutedText; font.bold: true }
            Label {
                text: root.valueText(vehicleModel.attitudeValid,
                                     vehicleModel.pitchRad * 180.0 / Math.PI, " deg", 1)
            }
            Label { text: "Yaw"; color: animusTheme.mutedText; font.bold: true }
            Label {
                text: root.valueText(vehicleModel.attitudeValid,
                                     vehicleModel.yawRad * 180.0 / Math.PI, " deg", 1)
            }
            Label { text: "Heading"; color: animusTheme.mutedText; font.bold: true }
            Label {
                text: root.valueText(vehicleModel.attitudeValid, vehicleModel.headingDeg, " deg", 1)
            }
            Label { text: "Rates"; color: animusTheme.mutedText; font.bold: true }
            Label {
                text: root.valueText(vehicleModel.attitudeValid,
                                     vehicleModel.rollRateRps * 180.0 / Math.PI, " R", 1) + " " +
                      root.valueText(vehicleModel.attitudeValid,
                                     vehicleModel.pitchRateRps * 180.0 / Math.PI, " P", 1) + " " +
                      root.valueText(vehicleModel.attitudeValid,
                                     vehicleModel.yawRateRps * 180.0 / Math.PI, " Y", 1)
            }
            Label { text: "Link"; color: animusTheme.mutedText; font.bold: true }
            AnimusStatusBadge {
                text: (!telemetryService.running && telemetryService.decodedSampleCount === 0)
                      ? "UNK" : (telemetryService.linkFresh ? "FRESH" : "STALE")
                tone: telemetryService.linkFresh ? "success" : "warning"
            }
        }
    }

    TelemetryStrip {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
    }

    AnimusSceneStatus {
        objectName: "tacticalSceneStatusOverlay"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        workspaceLabel: "Tactical"
        sceneStatus: root.localSceneStatus
        webSceneActive: webLoader.active
        webSceneReady: webLoader.status === Loader.Ready && webLoader.item !== null
        webSceneError: webLoader.status === Loader.Error
        fallbackActive: root.useFallbackScene()
    }

    AnimusIconButton {
        objectName: "tacticalSnapButton"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        text: "\u21ba"
        toolTipText: "Reset tactical camera"
        onClicked: root.resetCamera()
    }
}
