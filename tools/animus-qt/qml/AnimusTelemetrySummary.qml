import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AnimusOverlayPanel {
    id: root

    property bool detailsExpanded: false

    width: Math.min(parent ? parent.width - 24 : implicitWidth, root.detailsExpanded ? 610 : 500)
    height: root.detailsExpanded ? 104 : 40
    borderColor: root.primaryToneColor()
    borderWidth: telemetryService.linkFresh ? 1 : 2

    function primaryToneColor() {
        if (!telemetryService.linkFresh)
            return animusTheme.warning
        if (!vehicleModel.positionValid || !vehicleModel.velocityValid ||
                !vehicleModel.attitudeValid || !vehicleModel.gpsValid ||
                vehicleModel.gpsFixType < 3) {
            return animusTheme.warning
        }
        return animusTheme.success
    }

    function linkText() {
        if (!telemetryService.running && telemetryService.decodedSampleCount === 0)
            return "UNK"
        return telemetryService.linkFresh ? "LINK" : "STALE"
    }

    function gpsText() {
        if (!telemetryService.linkFresh)
            return "STALE"
        if (!vehicleModel.gpsValid)
            return "GPS UNK"
        if (vehicleModel.gpsFixType < 3)
            return "NO FIX"
        return "GPS " + vehicleModel.satellitesVisible + " SAT"
    }

    function metricText(valid, value, suffix, digits) {
        if (!telemetryService.linkFresh)
            return "STALE"
        if (!valid || !isFinite(Number(value)))
            return "--"
        return Number(value).toFixed(digits) + suffix
    }

    function attitudeText() {
        if (!telemetryService.linkFresh)
            return "STALE"
        if (!vehicleModel.attitudeValid)
            return "ATT UNK"
        return "R " + (vehicleModel.rollRad * 180.0 / Math.PI).toFixed(0) +
               " P " + (vehicleModel.pitchRad * 180.0 / Math.PI).toFixed(0)
    }

    function valueColor(valid) {
        return valid && telemetryService.linkFresh ? animusTheme.text : animusTheme.warning
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AnimusStatusBadge {
                text: root.linkText()
                tone: telemetryService.linkFresh ? "success" : "warning"
            }

            AnimusStatusBadge {
                text: root.gpsText()
                tone: telemetryService.linkFresh && vehicleModel.gpsValid &&
                      vehicleModel.gpsFixType >= 3 ? "success" : "warning"
            }

            Label {
                text: root.metricText(vehicleModel.positionValid,
                                      vehicleModel.altitudeM, " m MSL", 0)
                color: root.valueColor(vehicleModel.positionValid)
                font.pixelSize: 13
                font.bold: true
            }

            Label {
                text: root.metricText(vehicleModel.velocityValid,
                                      vehicleModel.groundspeedMps, " m/s GS", 1)
                color: root.valueColor(vehicleModel.velocityValid)
                font.pixelSize: 13
                font.bold: true
            }

            Label {
                text: root.metricText(vehicleModel.velocityValid,
                                      -vehicleModel.vzDownMps, " m/s VS", 1)
                color: root.valueColor(vehicleModel.velocityValid)
                font.pixelSize: 13
            }

            Item { Layout.fillWidth: true }

            Label {
                text: root.attitudeText()
                color: root.valueColor(vehicleModel.attitudeValid)
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.maximumWidth: 120
            }
        }

        GridLayout {
            Layout.fillWidth: true
            visible: root.detailsExpanded
            columns: 4
            rowSpacing: 4
            columnSpacing: 10

            Label {
                text: "POS"
                color: animusTheme.mutedText
                font.pixelSize: 11
                font.bold: true
            }
            Label {
                Layout.preferredWidth: 170
                text: telemetryService.linkFresh && vehicleModel.positionValid
                      ? vehicleModel.latitudeDeg.toFixed(5) + ", " +
                        vehicleModel.longitudeDeg.toFixed(5)
                      : (telemetryService.linkFresh ? "UNK" : "STALE")
                color: root.valueColor(vehicleModel.positionValid)
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Label {
                text: "HDG"
                color: animusTheme.mutedText
                font.pixelSize: 11
                font.bold: true
            }
            Label {
                text: root.metricText(vehicleModel.attitudeValid,
                                      vehicleModel.headingDeg, " deg", 0)
                color: root.valueColor(vehicleModel.attitudeValid)
                font.pixelSize: 12
            }

            Label {
                text: "ALT"
                color: animusTheme.mutedText
                font.pixelSize: 11
                font.bold: true
            }
            Label {
                text: root.metricText(vehicleModel.positionValid,
                                      vehicleModel.altitudeM, " m", 1)
                color: root.valueColor(vehicleModel.positionValid)
                font.pixelSize: 12
            }

            Label {
                text: "VEL"
                color: animusTheme.mutedText
                font.pixelSize: 11
                font.bold: true
            }
            Label {
                text: root.metricText(vehicleModel.velocityValid,
                                      vehicleModel.groundspeedMps, " m/s", 1) +
                      " / " +
                      root.metricText(vehicleModel.velocityValid,
                                      -vehicleModel.vzDownMps, " m/s", 1)
                color: root.valueColor(vehicleModel.velocityValid)
                font.pixelSize: 12
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.detailsExpanded = !root.detailsExpanded
    }
}
