import QtQuick
import QtWebEngine

WebEngineView {
    id: webView
    objectName: "terrain3DWebView"

    signal captureFinished(bool ok, string error)
    signal controlSurfaceInspectionFinished(bool ok, string error)
    signal cameraModeChanged(string mode)

    property string workspaceMode: "terrain-3d"
    property var sceneStatus: ({ "status": "initializing", "error": "" })
    property string pendingControlSurfaceDiagnosticPath: ""
    property string pendingControlSurfaceSnapshotJson: ""
    property int pendingControlSurfaceDiagnosticAttempts: 0
    property int maxControlSurfaceDiagnosticAttempts: 100

    function setLocalSceneStatus(status, error) {
        webView.sceneStatus = {
            "status": status || "scene-status",
            "error": error || ""
        }
    }

    function pushSnapshot() {
        var payload = JSON.stringify(cesiumBridge.snapshot())
        runJavaScript("window.animusSetWorkspaceMode && window.animusSetWorkspaceMode(" +
                      JSON.stringify(webView.workspaceMode) + ")")
        runJavaScript("window.animusApplySnapshot && window.animusApplySnapshot(" + payload + ")")
    }

    function captureCesiumPng(path) {
        runJavaScript("window.animusCaptureCesiumPng && window.animusCaptureCesiumPng()",
                      function(result) {
                          if (!result) {
                              webView.captureFinished(false, "Cesium canvas capture returned no PNG")
                              return
                          }
                          if (!captureWriter.writePngDataUrl(path, String(result))) {
                              webView.captureFinished(false, "failed to write Cesium canvas PNG")
                              return
                          }
                          webView.captureFinished(true, "")
                      })
    }

    function inspectControlSurfaces(path, snapshot) {
        webView.pendingControlSurfaceDiagnosticPath = path
        webView.pendingControlSurfaceSnapshotJson = snapshot ? JSON.stringify(snapshot) : ""
        webView.pendingControlSurfaceDiagnosticAttempts = 0
        if (snapshot)
            runJavaScript("window.animusApplySnapshot && window.animusApplySnapshot(" +
                          webView.pendingControlSurfaceSnapshotJson + ")")
        controlSurfaceInspectionTimer.restart()
    }

    function finishControlSurfaceInspection(result, finalAttempt) {
        if (!result) {
            if (!finalAttempt)
                return
            webView.controlSurfaceInspectionFinished(
                false, "control-surface inspection returned no result")
            return
        }
        if (!result.ok && !finalAttempt)
            return
        controlSurfaceInspectionTimer.stop()
        var json = JSON.stringify(result, null, 2) + "\n"
        if (!captureWriter.writeTextFile(webView.pendingControlSurfaceDiagnosticPath, json)) {
            webView.controlSurfaceInspectionFinished(
                false, "failed to write control-surface diagnostic")
            return
        }
        webView.controlSurfaceInspectionFinished(!!result.ok, "")
    }

    Timer {
        id: controlSurfaceInspectionTimer
        interval: 150
        repeat: true
        onTriggered: {
            webView.pendingControlSurfaceDiagnosticAttempts += 1
            var finalAttempt =
                    webView.pendingControlSurfaceDiagnosticAttempts >=
                    webView.maxControlSurfaceDiagnosticAttempts
            var script = "(function() {"
            if (webView.pendingControlSurfaceSnapshotJson.length > 0) {
                script += "if (window.animusApplySnapshot) window.animusApplySnapshot(" +
                        webView.pendingControlSurfaceSnapshotJson + ");"
            }
            script += "return window.animusInspectControlSurfaces && window.animusInspectControlSurfaces();"
            script += "})()"
            runJavaScript(script,
                          function(result) {
                              webView.finishControlSurfaceInspection(result, finalAttempt)
                          })
            if (finalAttempt)
                stop()
        }
    }

    function inspectCameraState(path) {
        runJavaScript("window.animusCameraState && window.animusCameraState()",
                      function(result) {
                          if (!result) {
                              webView.controlSurfaceInspectionFinished(
                                  false, "camera state inspection returned no result")
                              return
                          }
                          var json = JSON.stringify(result, null, 2) + "\n"
                          if (!captureWriter.writeTextFile(path, json)) {
                              webView.controlSurfaceInspectionFinished(
                                  false, "failed to write camera state diagnostic")
                              return
                          }
                          webView.controlSurfaceInspectionFinished(!!result.ok, "")
                      })
    }

    function setCameraMode(mode) {
        runJavaScript("window.animusSetCameraMode && window.animusSetCameraMode(" +
                      JSON.stringify(mode) + ")")
    }

    function resetTacticalCamera() {
        runJavaScript("window.animusResetTacticalCamera && window.animusResetTacticalCamera()")
    }

    settings.localContentCanAccessFileUrls: true
    settings.localContentCanAccessRemoteUrls: false
    url: "qrc:/Animus/web/cesium/index.html"

    onLoadingChanged: function(loadRequest) {
        if (loadRequest.status === WebEngineView.LoadSucceededStatus) {
            setLocalSceneStatus("webengine-ready", "")
            pushSnapshot()
        } else if (loadRequest.status === WebEngineView.LoadFailedStatus) {
            setLocalSceneStatus("webengine-error", loadRequest.errorString)
        }
    }

    onJavaScriptConsoleMessage: function(level, message, lineNumber, sourceID) {
        var modePrefix = "ANIMUS_CAMERA_MODE "
        if (message.indexOf(modePrefix) === 0) {
            try {
                var camera = JSON.parse(message.substring(modePrefix.length))
                if (camera.mode)
                    webView.cameraModeChanged(String(camera.mode))
            } catch (error) {
                setLocalSceneStatus("camera-mode-error", String(error))
            }
            return
        }

        var prefix = "ANIMUS_SCENE_STATUS "
        if (message.indexOf(prefix) !== 0)
            return
        try {
            var scene = JSON.parse(message.substring(prefix.length))
            setLocalSceneStatus(scene.status || "scene-status", scene.error || "")
        } catch (error) {
            setLocalSceneStatus("scene-status-error", String(error))
        }
    }

    Connections {
        target: cesiumBridge
        function onLatestVehicleChanged(vehicle) { webView.pushSnapshot() }
        function onHomeChanged(home) { webView.pushSnapshot() }
        function onTrailChanged(trail) { webView.pushSnapshot() }
        function onTerrainStatusChanged(terrain) { webView.pushSnapshot() }
    }
}
