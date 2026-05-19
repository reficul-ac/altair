import QtQuick
import QtWebEngine

WebEngineView {
    id: webView
    objectName: "terrain3DWebView"

    signal captureFinished(bool ok, string error)

    function pushSnapshot() {
        var payload = JSON.stringify(cesiumBridge.snapshot())
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

    function setCameraMode(mode) {
        runJavaScript("window.animusSetCameraMode && window.animusSetCameraMode(" +
                      JSON.stringify(mode) + ")")
    }

    settings.localContentCanAccessFileUrls: true
    settings.localContentCanAccessRemoteUrls: false
    url: "qrc:/Animus/web/cesium/index.html"

    onLoadingChanged: function(loadRequest) {
        if (loadRequest.status === WebEngineView.LoadSucceededStatus) {
            cesiumBridge.setSceneStatus("webengine-ready", "")
            pushSnapshot()
        } else if (loadRequest.status === WebEngineView.LoadFailedStatus) {
            cesiumBridge.setSceneStatus("webengine-error", loadRequest.errorString)
        }
    }

    onJavaScriptConsoleMessage: function(level, message, lineNumber, sourceID) {
        var prefix = "ANIMUS_SCENE_STATUS "
        if (message.indexOf(prefix) !== 0)
            return
        try {
            var scene = JSON.parse(message.substring(prefix.length))
            cesiumBridge.setSceneStatus(scene.status || "scene-status", scene.error || "")
        } catch (error) {
            cesiumBridge.setSceneStatus("scene-status-error", String(error))
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
