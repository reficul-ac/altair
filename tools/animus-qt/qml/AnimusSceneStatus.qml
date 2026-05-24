import QtQuick
import QtQuick.Controls

AnimusOverlayPanel {
    id: root

    property string workspaceLabel: "Scene"
    property var sceneStatus: ({ "status": "initializing", "error": "" })
    property var terrainStatus: ({})
    property bool webSceneActive: true
    property bool webSceneReady: false
    property bool webSceneError: false
    property bool fallbackActive: false
    property bool showHealthy: false

    function sceneState() {
        if (!root.sceneStatus)
            return "initializing"
        return root.sceneStatus.status || "initializing"
    }

    function sceneDetail() {
        if (!root.sceneStatus)
            return ""
        return root.sceneStatus.error || ""
    }

    function isHealthyDetail(status, detail) {
        if (!detail)
            return true
        if (status === "terrain-ready")
            return detail === "local heightmap fixture" ||
                   detail === "local quantized-mesh terrain"
        if (status === "tactical-ready")
            return detail === "vehicle-locked attitude view"
        return false
    }

    function isHealthyStatus() {
        var status = root.sceneState()
        if (status === "webengine-ready" || status === "cesium-ready" ||
                status === "terrain-ready" || status === "tactical-ready" ||
                status === "fpv-ready") {
            return root.isHealthyDetail(status, root.sceneDetail())
        }
        return false
    }

    function needsAttention() {
        var status = root.sceneState()
        if (!root.webSceneActive || root.webSceneError || !root.webSceneReady ||
                root.fallbackActive)
            return true
        if (status === "initializing")
            return true
        if (status.indexOf("error") >= 0 || status.indexOf("fallback") >= 0 ||
                status.indexOf("stale") >= 0 || status.indexOf("degraded") >= 0 ||
                status.indexOf("loading") >= 0)
            return true
        return !root.isHealthyStatus()
    }

    function terrainSourceText() {
        if (!root.terrainStatus || !root.terrainStatus.provider)
            return ""
        if (root.terrainStatus.provider === "quantized-mesh") {
            if (root.terrainStatus.cachePath)
                return "terrain source: quantized mesh " + root.terrainStatus.cachePath
            return "terrain source: quantized mesh"
        }
        if (root.terrainStatus.fixture && root.terrainStatus.fixture.name)
            return "terrain source: fixture " + root.terrainStatus.fixture.name
        return "terrain source: " + root.terrainStatus.provider
    }

    function statusText() {
        var status = root.sceneState()
        var detail = root.sceneDetail()
        var prefix = root.workspaceLabel + " scene"
        var terrain = root.terrainSourceText()
        var suffix = terrain ? " | " + terrain : ""

        if (!root.webSceneActive)
            return prefix + " fallback: WebEngine disabled" + suffix
        if (root.webSceneError)
            return prefix + " fallback: WebEngine load failed" + suffix
        if (!root.webSceneReady && status === "initializing")
            return prefix + " initializing" + suffix
        if (detail && !root.isHealthyDetail(status, detail))
            return prefix + " " + status + ": " + detail + suffix
        return prefix + " " + status + suffix
    }

    function toneColor() {
        var status = root.sceneState()
        if (root.webSceneError || status.indexOf("error") >= 0)
            return animusTheme.danger
        if (root.fallbackActive || status.indexOf("fallback") >= 0 ||
                status.indexOf("stale") >= 0 ||
                status.indexOf("degraded") >= 0)
            return animusTheme.warning
        return animusTheme.border
    }

    function emphasizedBorder() {
        var status = root.sceneState()
        return root.webSceneError || root.fallbackActive ||
               status.indexOf("error") >= 0 ||
               status.indexOf("fallback") >= 0 ||
               status.indexOf("stale") >= 0 ||
               status.indexOf("degraded") >= 0
    }

    visible: root.showHealthy || root.needsAttention()
    borderColor: root.toneColor()
    borderWidth: root.emphasizedBorder() ? 2 : 1

    Label {
        text: root.statusText()
        color: animusTheme.text
        wrapMode: Text.Wrap
        width: Math.min(implicitWidth, 420)
    }
}
