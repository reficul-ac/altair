import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root

    width: Math.min(parent ? parent.width - 24 : implicitWidth, 560)
    padding: 8
    background: Rectangle {
        color: animusTheme.overlay
        opacity: 0.94
        border.color: telemetryService.linkFresh ? animusTheme.success : animusTheme.warning
        radius: 6
    }

    function valueText(valid, value, suffix, digits) {
        if (!telemetryService.linkFresh)
            return "STALE"
        if (!valid)
            return "UNK"
        return Number(value).toFixed(digits) + suffix
    }

    function gpsText() {
        if (!telemetryService.linkFresh)
            return "STALE"
        if (!vehicleModel.gpsValid)
            return "UNK"
        if (vehicleModel.gpsFixType < 3)
            return "NO FIX"
        return "FIX " + vehicleModel.satellitesVisible + " SAT"
    }

    function linkText() {
        if (!telemetryService.running && telemetryService.decodedSampleCount === 0)
            return "UNK"
        return telemetryService.linkFresh ? "FRESH" : "STALE"
    }

    GridLayout {
        anchors.fill: parent
        columns: 4
        rowSpacing: 4
        columnSpacing: 10

        Label {
            text: "ATT"
            color: animusTheme.mutedText
            font.pixelSize: 11
            font.bold: true
        }
        Label {
            text: root.valueText(vehicleModel.attitudeValid,
                                 vehicleModel.rollRad * 180.0 / Math.PI, " R", 0) + " " +
                  root.valueText(vehicleModel.attitudeValid,
                                 vehicleModel.pitchRad * 180.0 / Math.PI, " P", 0)
            color: vehicleModel.attitudeValid && telemetryService.linkFresh ? animusTheme.text : animusTheme.warning
            font.pixelSize: 12
        }

        Label {
            text: "ALT"
            color: animusTheme.mutedText
            font.pixelSize: 11
            font.bold: true
        }
        Label {
            text: root.valueText(vehicleModel.positionValid, vehicleModel.altitudeM, " m", 1)
            color: vehicleModel.positionValid && telemetryService.linkFresh ? animusTheme.text : animusTheme.warning
            font.pixelSize: 12
        }

        Label {
            text: "POS"
            color: animusTheme.mutedText
            font.pixelSize: 11
            font.bold: true
        }
        Label {
            Layout.preferredWidth: 160
            text: telemetryService.linkFresh && vehicleModel.positionValid
                  ? vehicleModel.latitudeDeg.toFixed(5) + ", " + vehicleModel.longitudeDeg.toFixed(5)
                  : (telemetryService.linkFresh ? "UNK" : "STALE")
            color: vehicleModel.positionValid && telemetryService.linkFresh ? animusTheme.text : animusTheme.warning
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        Label {
            text: "VEL"
            color: animusTheme.mutedText
            font.pixelSize: 11
            font.bold: true
        }
        Label {
            text: root.valueText(vehicleModel.velocityValid,
                                 vehicleModel.groundspeedMps, " m/s GS", 1) + " " +
                  root.valueText(vehicleModel.velocityValid,
                                 -vehicleModel.vzDownMps, " VS", 1)
            color: vehicleModel.velocityValid && telemetryService.linkFresh ? animusTheme.text : animusTheme.warning
            font.pixelSize: 12
        }

        Label {
            text: "GPS"
            color: animusTheme.mutedText
            font.pixelSize: 11
            font.bold: true
        }
        Label {
            text: root.gpsText()
            color: vehicleModel.gpsValid && telemetryService.linkFresh &&
                   vehicleModel.gpsFixType >= 3 ? animusTheme.text : animusTheme.warning
            font.pixelSize: 12
        }

        Label {
            text: "LINK"
            color: animusTheme.mutedText
            font.pixelSize: 11
            font.bold: true
        }
        Label {
            text: root.linkText()
            color: telemetryService.linkFresh ? animusTheme.success : animusTheme.warning
            font.pixelSize: 12
            font.bold: true
        }
    }
}
