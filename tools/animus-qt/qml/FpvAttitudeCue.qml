import QtQuick

Item {
    id: root

    property real rollRad: 0.0
    property real pitchRad: 0.0
    property bool valid: false

    width: 260
    height: 170

    Canvas {
        id: cueCanvas
        anchors.fill: parent
        opacity: root.valid ? 0.86 : 0.46

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var cx = width * 0.5
            var cy = height * 0.5
            var pitchOffset = Math.max(-34, Math.min(34, -root.pitchRad * 180.0 / Math.PI * 1.15))

            ctx.save()
            ctx.translate(cx, cy)
            ctx.rotate(-root.rollRad)
            ctx.strokeStyle = animusTheme.surface
            ctx.lineWidth = 5
            ctx.beginPath()
            ctx.moveTo(-88, pitchOffset)
            ctx.lineTo(-26, pitchOffset)
            ctx.moveTo(26, pitchOffset)
            ctx.lineTo(88, pitchOffset)
            ctx.stroke()
            ctx.strokeStyle = animusTheme.text
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(-88, pitchOffset)
            ctx.lineTo(-26, pitchOffset)
            ctx.moveTo(26, pitchOffset)
            ctx.lineTo(88, pitchOffset)
            ctx.stroke()
            ctx.restore()

            ctx.strokeStyle = animusTheme.surface
            ctx.lineWidth = 5
            ctx.beginPath()
            ctx.moveTo(cx - 14, cy)
            ctx.lineTo(cx - 4, cy)
            ctx.moveTo(cx + 4, cy)
            ctx.lineTo(cx + 14, cy)
            ctx.moveTo(cx, cy - 14)
            ctx.lineTo(cx, cy - 4)
            ctx.moveTo(cx, cy + 4)
            ctx.lineTo(cx, cy + 14)
            ctx.stroke()

            ctx.strokeStyle = animusTheme.success
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(cx - 14, cy)
            ctx.lineTo(cx - 4, cy)
            ctx.moveTo(cx + 4, cy)
            ctx.lineTo(cx + 14, cy)
            ctx.moveTo(cx, cy - 14)
            ctx.lineTo(cx, cy - 4)
            ctx.moveTo(cx, cy + 4)
            ctx.lineTo(cx, cy + 14)
            ctx.stroke()

            ctx.strokeStyle = root.valid ? animusTheme.text : animusTheme.warning
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.arc(cx, cy, 42, Math.PI * 0.12, Math.PI * 0.88)
            ctx.stroke()
        }
    }

    Connections {
        target: animusTheme
        function onThemeChanged() { cueCanvas.requestPaint() }
    }

    onRollRadChanged: cueCanvas.requestPaint()
    onPitchRadChanged: cueCanvas.requestPaint()
    onValidChanged: cueCanvas.requestPaint()
}
