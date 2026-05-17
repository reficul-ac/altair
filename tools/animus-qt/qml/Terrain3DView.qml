import QtQuick
import QtQuick.Controls

Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: "#d8e2e0"
    }

    Canvas {
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var sky = ctx.createLinearGradient(0, 0, 0, height * 0.58)
            sky.addColorStop(0, "#9fb8d0")
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

    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        text: "Offline terrain preview: " + vehicleModel.latitudeDeg.toFixed(5) + ", " + vehicleModel.longitudeDeg.toFixed(5)
        padding: 8
        background: Rectangle { color: "#f7f7f3"; border.color: "#c9c9c0"; radius: 6 }
    }
}
