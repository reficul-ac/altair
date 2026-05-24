import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AnimusOverlayPanel {
    id: root

    property string sceneMode: "terrain"
    property var clearance: ({})
    property string cameraState: ""
    property bool cameraLocked: true
    property bool resetAvailable: false
    property bool detailsExpanded: false

    function clearanceState() {
        return root.clearance && root.clearance.state ? root.clearance.state : "unknown"
    }

    function isDegraded() {
        var state = root.clearanceState()
        return state === "warning" || state === "caution" || state === "unknown"
    }

    function metricText(key, suffix) {
        if (root.clearanceState() === "unknown" || !root.clearance ||
                !isFinite(Number(root.clearance[key]))) {
            return "--"
        }
        return Number(root.clearance[key]).toFixed(1) + suffix
    }

    function tone() {
        var state = root.clearanceState()
        if (state === "warning")
            return "danger"
        if (state === "caution")
            return "warning"
        if (state === "clear")
            return "success"
        return "neutral"
    }

    function toneColor() {
        var currentTone = root.tone()
        if (currentTone === "danger")
            return animusTheme.danger
        if (currentTone === "warning")
            return animusTheme.warning
        if (currentTone === "success")
            return animusTheme.success
        return animusTheme.border
    }

    function trendText() {
        var trend = Number(root.clearance && root.clearance.trendMps)
        if (root.clearanceState() === "unknown" || !isFinite(trend))
            return "--"
        if (Math.abs(trend) < 0.05)
            return "level"
        return (trend > 0.0 ? "climbing " : "closing ") + Math.abs(trend).toFixed(1) + " m/s"
    }

    function cameraText() {
        if (root.sceneMode === "fpv")
            return root.cameraLocked ? "FPV locked" : "FPV unlocked"
        if (root.cameraState.length > 0)
            return root.cameraState
        return root.cameraLocked ? "camera locked" : "camera free"
    }

    width: Math.min(parent ? parent.width - 24 : implicitWidth, root.sceneMode === "fpv" ? 430 : 520)
    height: root.sceneMode === "fpv" ? 112 : 86
    borderColor: root.toneColor()
    borderWidth: root.isDegraded() ? 2 : 1

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 4
        width: 10
        radius: 2
        color: root.toneColor()
        opacity: root.tone() === "neutral" ? 0.32 : 0.88
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            AnimusStatusBadge {
                text: root.clearanceState().toUpperCase()
                tone: root.tone()
            }

            Label {
                text: "AGL " + root.metricText("aglM", " m")
                color: animusTheme.text
                font.pixelSize: 12
                font.bold: true
            }

            Label {
                text: "MIN " + root.metricText("minimumRecentClearanceM", " m")
                color: animusTheme.text
                font.pixelSize: 12
            }

            Label {
                text: root.trendText().toUpperCase()
                color: root.clearanceState() === "clear" ? animusTheme.mutedText : root.toneColor()
                font.pixelSize: 12
                font.bold: root.clearanceState() !== "clear"
            }

            Item { Layout.fillWidth: true }

            Label {
                text: root.cameraText()
                color: animusTheme.mutedText
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.maximumWidth: 140
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: true

            Label {
                text: root.sceneMode === "fpv"
                      ? "HDG " + (vehicleModel.attitudeValid
                                   ? vehicleModel.headingDeg.toFixed(0) + " deg"
                                   : "--")
                      : "TERRAIN REF"
                color: vehicleModel.attitudeValid && telemetryService.linkFresh
                       ? animusTheme.text : animusTheme.warning
                font.pixelSize: 12
            }
            Label {
                text: root.sceneMode === "fpv"
                      ? "PITCH " + (vehicleModel.attitudeValid
                                     ? (vehicleModel.pitchRad * 180.0 / Math.PI).toFixed(0) + " deg"
                                     : "--")
                      : "rings 100/250/500 m"
                color: vehicleModel.attitudeValid && telemetryService.linkFresh
                       ? animusTheme.text : animusTheme.warning
                font.pixelSize: 12
            }
            Label {
                text: root.sceneMode === "fpv"
                      ? (root.resetAvailable ? "reset ready" : "reset unavailable")
                      : "route/trail visible"
                color: root.sceneMode === "fpv" && !root.resetAvailable
                       ? animusTheme.warning : animusTheme.mutedText
                font.pixelSize: 12
            }
        }

        Label {
            visible: root.detailsExpanded || root.isDegraded()
            Layout.fillWidth: true
            text: (root.clearance && root.clearance.message) ? root.clearance.message
                                                             : "terrain clearance unavailable"
            color: animusTheme.mutedText
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.detailsExpanded = !root.detailsExpanded
    }
}
