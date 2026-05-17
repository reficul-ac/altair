import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property bool following: true
    readonly property double centerLatitudeDeg: following ? vehicleModel.latitudeDeg : 37.4275
    readonly property double centerLongitudeDeg: following ? vehicleModel.longitudeDeg : -122.1697
    readonly property double degreesPerPixel: 0.000014

    function projectX(longitudeDeg) {
        return width / 2 + (longitudeDeg - centerLongitudeDeg) / degreesPerPixel
    }

    function projectY(latitudeDeg) {
        return height / 2 - (latitudeDeg - centerLatitudeDeg) / degreesPerPixel
    }

    Rectangle {
        anchors.fill: parent
        color: "#dfe7de"

        Repeater {
            model: 16
            Rectangle {
                width: parent.width
                height: 1
                y: index * parent.height / 15
                color: "#c6d0c5"
            }
        }

        Repeater {
            model: 24
            Rectangle {
                width: 1
                height: parent.height
                x: index * parent.width / 23
                color: "#c6d0c5"
            }
        }

        Rectangle {
            x: parent.width * 0.08
            y: parent.height * 0.58
            width: parent.width * 0.84
            height: 28
            radius: 14
            rotation: -11
            color: "#b9c7bd"
            opacity: 0.85
        }

        Rectangle {
            x: parent.width * 0.2
            y: parent.height * 0.22
            width: parent.width * 0.44
            height: 22
            radius: 11
            rotation: 31
            color: "#ccd7cb"
            opacity: 0.8
        }
    }

    Repeater {
        model: breadcrumbModel
        delegate: Rectangle {
            width: 7
            height: 7
            radius: 3.5
            x: root.projectX(longitude) - width / 2
            y: root.projectY(latitude) - height / 2
            color: "#1d6fd6"
            opacity: 0.42
        }
    }

    Rectangle {
        id: homeMarker
        width: 24
        height: 24
        radius: 12
        x: root.projectX(vehicleModel.homeLongitudeDeg) - width / 2
        y: root.projectY(vehicleModel.homeLatitudeDeg) - height / 2
        color: "#f7f7f3"
        border.color: "#b32020"
        border.width: 2

        Label {
            anchors.centerIn: parent
            text: "H"
            color: "#b32020"
            font.bold: true
        }
    }

    Item {
        id: vehicleMarker
        width: 42
        height: 42
        x: root.projectX(vehicleModel.longitudeDeg) - width / 2
        y: root.projectY(vehicleModel.latitudeDeg) - height / 2
        rotation: vehicleModel.headingDeg

        Canvas {
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.beginPath()
                ctx.moveTo(width / 2, 3)
                ctx.lineTo(width - 5, height - 5)
                ctx.lineTo(width / 2, height - 13)
                ctx.lineTo(5, height - 5)
                ctx.closePath()
                ctx.fillStyle = "#1d6fd6"
                ctx.strokeStyle = "white"
                ctx.lineWidth = 3
                ctx.fill()
                ctx.stroke()
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: root.following = false
    }

    Frame {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        background: Rectangle { color: "#f7f7f3"; border.color: "#c9c9c0"; radius: 6 }
        RowLayout {
            Label { text: offlineMaps.modeLabel() }
            Label {
                text: "CI offline renderer"
                color: "#4b5563"
            }
            Button {
                text: "Snap"
                onClicked: root.following = true
            }
        }
    }

    Label {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        text: (mapPacks.activeAttribution() || mapSources.activeAttribution()) + " | QtQuick fallback"
        color: "#202020"
        padding: 6
        background: Rectangle { color: "#f7f7f3"; opacity: 0.9; radius: 4 }
    }
}
