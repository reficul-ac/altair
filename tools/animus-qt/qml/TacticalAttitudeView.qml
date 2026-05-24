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

    function boundedRate(value) {
        var deg = Math.abs(value * 180.0 / Math.PI)
        return Math.min(deg / 90.0, 1.0)
    }

    function headingText() {
        return root.valueText(vehicleModel.attitudeValid, vehicleModel.headingDeg, " deg", 0)
    }

    function linkText() {
        if (!telemetryService.running && telemetryService.decodedSampleCount === 0)
            return "UNK"
        return telemetryService.linkFresh ? "FRESH" : "STALE"
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

            var cx = width * 0.5
            var cy = height * 0.52
            var scale = Math.min(width, height)
            var outer = scale * 0.24
            ctx.save()
            ctx.translate(cx, cy)

            ctx.strokeStyle = animusTheme.border
            ctx.globalAlpha = 0.42
            ctx.lineWidth = 1
            for (var grid = 1; grid <= 3; ++grid) {
                ctx.beginPath()
                ctx.arc(0, 0, outer * grid / 3.0, 0, Math.PI * 2)
                ctx.stroke()
            }
            for (var spoke = 0; spoke < 8; ++spoke) {
                var angle = spoke * Math.PI / 4.0
                ctx.beginPath()
                ctx.moveTo(Math.cos(angle) * outer * 0.34, Math.sin(angle) * outer * 0.34)
                ctx.lineTo(Math.cos(angle) * outer, Math.sin(angle) * outer)
                ctx.stroke()
            }

            var colors = [animusTheme.danger, animusTheme.success, animusTheme.accent]
            ctx.globalAlpha = 0.86
            ctx.lineWidth = 3
            for (var i = 0; i < colors.length; ++i) {
                ctx.strokeStyle = colors[i]
                ctx.beginPath()
                ctx.arc(0, 0, outer * (0.52 + i * 0.16), 0, Math.PI * 2)
                ctx.stroke()
            }

            var headingRad = (vehicleModel.headingDeg - 90.0) * Math.PI / 180.0
            ctx.fillStyle = animusTheme.text
            ctx.globalAlpha = 0.88
            ctx.beginPath()
            ctx.moveTo(Math.cos(headingRad) * (outer + 16), Math.sin(headingRad) * (outer + 16))
            ctx.lineTo(Math.cos(headingRad + 0.10) * (outer + 4),
                       Math.sin(headingRad + 0.10) * (outer + 4))
            ctx.lineTo(Math.cos(headingRad - 0.10) * (outer + 4),
                       Math.sin(headingRad - 0.10) * (outer + 4))
            ctx.closePath()
            ctx.fill()

            ctx.globalAlpha = 1.0
            ctx.rotate(vehicleModel.rollRad)

            ctx.fillStyle = animusTheme.surface
            ctx.strokeStyle = animusTheme.text
            ctx.lineWidth = 3
            ctx.beginPath()
            ctx.moveTo(88, 0)
            ctx.lineTo(14, -13)
            ctx.lineTo(-66, -10)
            ctx.lineTo(-86, 0)
            ctx.lineTo(-66, 10)
            ctx.lineTo(14, 13)
            ctx.closePath()
            ctx.fill()
            ctx.stroke()

            ctx.fillStyle = animusTheme.accent
            ctx.beginPath()
            ctx.ellipse(28, -1, 14, 6, 0, 0, Math.PI * 2)
            ctx.fill()

            ctx.fillStyle = animusTheme.mutedText
            ctx.beginPath()
            ctx.moveTo(-6, -9)
            ctx.lineTo(-22, -68)
            ctx.lineTo(-37, -64)
            ctx.lineTo(-26, -7)
            ctx.closePath()
            ctx.fill()
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(-6, 9)
            ctx.lineTo(-22, 68)
            ctx.lineTo(-37, 64)
            ctx.lineTo(-26, 7)
            ctx.closePath()
            ctx.fill()
            ctx.stroke()

            ctx.fillStyle = animusTheme.border
            ctx.fillRect(-76, -28, 28, 8)
            ctx.fillRect(-76, 20, 28, 8)
            ctx.fillRect(-86, -6, 22, 12)
            ctx.restore()
        }
    }
    Connections {
        target: animusTheme
        function onThemeChanged() { fallbackCanvas.requestPaint() }
    }
    Connections {
        target: vehicleModel
        function onAttitudeChanged() { fallbackCanvas.requestPaint() }
        function onVehicleChanged() { fallbackCanvas.requestPaint() }
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
        id: tacticalAttitudeOverlay
        objectName: "tacticalAttitudeOverlay"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        width: Math.min(parent.width - 24, 390)
        padding: 12
        panelOpacity: 0.94
        borderColor: telemetryService.linkFresh ? animusTheme.success : animusTheme.warning

        ColumnLayout {
            anchors.fill: parent
            spacing: 9

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "TACTICAL"
                    color: animusTheme.mutedText
                    font.pixelSize: 11
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                AnimusStatusBadge {
                    text: "LOCK"
                    tone: "success"
                    ToolTip.visible: lockHover.containsMouse
                    ToolTip.text: "Vehicle-locked camera"

                    MouseArea {
                        id: lockHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }

                AnimusStatusBadge {
                    text: root.linkText()
                    tone: telemetryService.linkFresh ? "success" : "warning"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    spacing: 2
                    Layout.preferredWidth: 86
                    Label {
                        text: root.valueText(vehicleModel.attitudeValid,
                                             vehicleModel.rollRad * 180.0 / Math.PI, "", 1)
                        color: vehicleModel.attitudeValid && telemetryService.linkFresh
                               ? animusTheme.text : animusTheme.warning
                        font.pixelSize: 28
                        font.bold: true
                    }
                    Label {
                        text: "ROLL deg"
                        color: animusTheme.mutedText
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                ColumnLayout {
                    spacing: 2
                    Layout.preferredWidth: 86
                    Label {
                        text: root.valueText(vehicleModel.attitudeValid,
                                             vehicleModel.pitchRad * 180.0 / Math.PI, "", 1)
                        color: vehicleModel.attitudeValid && telemetryService.linkFresh
                               ? animusTheme.text : animusTheme.warning
                        font.pixelSize: 28
                        font.bold: true
                    }
                    Label {
                        text: "PITCH deg"
                        color: animusTheme.mutedText
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                ColumnLayout {
                    spacing: 2
                    Layout.fillWidth: true
                    Label {
                        text: root.headingText()
                        color: vehicleModel.attitudeValid && telemetryService.linkFresh
                               ? animusTheme.text : animusTheme.warning
                        font.pixelSize: 20
                        font.bold: true
                    }
                    Label {
                        text: "HDG/TRK"
                        color: animusTheme.mutedText
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 10
                rowSpacing: 4

                Repeater {
                    model: [
                        { axis: "R", rate: vehicleModel.rollRateRps },
                        { axis: "P", rate: vehicleModel.pitchRateRps },
                        { axis: "Y", rate: vehicleModel.yawRateRps }
                    ]
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: modelData.axis + " RATE"
                                color: animusTheme.mutedText
                                font.pixelSize: 10
                                font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: root.valueText(vehicleModel.attitudeValid,
                                                     modelData.rate * 180.0 / Math.PI, "", 0)
                                color: vehicleModel.attitudeValid && telemetryService.linkFresh
                                       ? animusTheme.text : animusTheme.warning
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 4
                            radius: 2
                            color: animusTheme.surface
                            Rectangle {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * root.boundedRate(modelData.rate)
                                height: parent.height
                                radius: 2
                                color: telemetryService.linkFresh ? animusTheme.accent
                                                                  : animusTheme.warning
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: animusTheme.border
                opacity: 0.65
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 4
                columnSpacing: 12

                Label {
                    text: "YAW"
                    color: animusTheme.mutedText
                    font.pixelSize: 10
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    text: root.valueText(vehicleModel.attitudeValid,
                                         vehicleModel.yawRad * 180.0 / Math.PI, " deg", 1)
                    color: vehicleModel.attitudeValid && telemetryService.linkFresh
                           ? animusTheme.text : animusTheme.warning
                    font.pixelSize: 12
                }
                Label {
                    text: "CAMERA"
                    color: animusTheme.mutedText
                    font.pixelSize: 10
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    text: "VEHICLE LOCKED"
                    color: animusTheme.text
                    font.pixelSize: 12
                    font.bold: true
                }
            }
        }
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
        id: tacticalSnapButton
        objectName: "tacticalSnapButton"
        anchors.left: tacticalAttitudeOverlay.right
        anchors.top: tacticalAttitudeOverlay.top
        anchors.leftMargin: 8
        width: 32
        height: 30
        text: "\u21ba"
        toolTipText: "Reset tactical camera"
        onClicked: root.resetCamera()

        background: Rectangle {
            color: tacticalSnapButton.down ? animusTheme.surface : animusTheme.overlay
            border.color: tacticalSnapButton.hovered ? animusTheme.accent : animusTheme.border
            border.width: 1
            opacity: 0.94
            radius: 4
        }
    }
}
