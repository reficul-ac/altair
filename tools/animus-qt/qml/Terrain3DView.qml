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
            return "#b42318"
        if (cesiumBridge.terrainClearance.state === "caution")
            return "#9a5b00"
        if (cesiumBridge.terrainClearance.state === "clear")
            return "#0f7b43"
        return "#4b5563"
    }

    function useFallbackScene() {
        var status = root.localSceneStatus.status || "initializing"
        return !webLoader.active || webLoader.status === Loader.Error ||
               status === "initializing" || status.indexOf("error") >= 0
    }

    Rectangle {
        anchors.fill: parent
        color: "#d7e3df"
    }

    Canvas {
        id: fallbackCanvas
        anchors.fill: parent
        visible: root.useFallbackScene()
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var sky = ctx.createLinearGradient(0, 0, 0, height * 0.58)
            sky.addColorStop(0, "#8fb1ce")
            sky.addColorStop(1, "#e6ece8")
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
                  "#879a83", "#61705f")
            ridge([[0, 0.78], [0.16, 0.69], [0.32, 0.73], [0.48, 0.62], [0.68, 0.7], [0.82, 0.59], [1, 0.73]],
                  "#5f775f", "#415542")

            ctx.strokeStyle = "#d7e6d0"
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

    Rectangle {
        width: 46
        height: 46
        radius: 23
        x: parent.width * 0.5 - width / 2
        y: parent.height * 0.42 - height / 2
        visible: fallbackCanvas.visible
        color: "#1d6fd6"
        border.color: "white"
        border.width: 3

        Rectangle {
            width: 4
            height: 70
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            color: "#1d6fd6"
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

    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        text: root.statusText()
        padding: 8
        color: "#1f2a2a"
        background: Rectangle {
            color: "#f7f7f3"
            border.color: "#c9c9c0"
            radius: 6
        }
    }

    TelemetryStrip {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
    }

    Frame {
        objectName: "terrainClearanceOverlay"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: Math.min(parent.width - 24, 370)
        background: Rectangle {
            color: "#f7f7f3"
            border.color: root.clearanceColor()
            border.width: 2
            radius: 6
        }

        Grid {
            columns: 2
            spacing: 8
            anchors.fill: parent

            Label {
                text: "Clearance"
                font.bold: true
                color: root.clearanceColor()
            }
            Label {
                text: cesiumBridge.terrainClearance.state || "unknown"
                color: root.clearanceColor()
                horizontalAlignment: Text.AlignRight
                width: 110
            }
            Label { text: "AGL"; color: "#4b5563" }
            Label {
                text: root.clearanceText(cesiumBridge.terrainClearance.aglM, " m")
                horizontalAlignment: Text.AlignRight
                width: 110
            }
            Label { text: "Min recent"; color: "#4b5563" }
            Label {
                text: root.clearanceText(cesiumBridge.terrainClearance.minimumRecentClearanceM,
                                         " m")
                horizontalAlignment: Text.AlignRight
                width: 110
            }
            Label { text: "Trend"; color: "#4b5563" }
            Label {
                text: root.clearanceText(cesiumBridge.terrainClearance.trendMps, " m/s")
                horizontalAlignment: Text.AlignRight
                width: 110
            }
            Label {
                text: cesiumBridge.terrainClearance.message || "terrain clearance unavailable"
                color: "#4b5563"
                width: 230
                elide: Text.ElideRight
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        spacing: 4

        Button {
            text: "Chase"
            checkable: true
            checked: root.cameraMode === "chase"
            onClicked: root.setCameraMode("chase")
        }
        Button {
            text: "Orbit"
            checkable: true
            checked: root.cameraMode === "orbit"
            onClicked: root.setCameraMode("orbit")
        }
        Button {
            text: "Free"
            checkable: true
            checked: root.cameraMode === "free"
            onClicked: root.setCameraMode("free")
        }
    }
}
