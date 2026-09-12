import QtQuick
import QtQuick.Window

// Folder / document glyph for the file browser rows, drawn the same way as the
// footer icons so both stay crisp on hidpi screens.
Item {
    id: control

    // "folder", "file" or "up"
    property string kind: "file"
    property color iconColor: "#666666"

    width: 16
    height: 16

    Canvas {
        id: iconCanvas

        readonly property real dpr: Screen.devicePixelRatio
        width: control.width * dpr
        height: control.height * dpr
        transformOrigin: Item.TopLeft
        scale: 1 / dpr
        onDprChanged: requestPaint()

        onPaint: {
            var context = getContext("2d");
            context.setTransform(dpr, 0, 0, dpr, 0, 0);
            context.clearRect(0, 0, width, height);
            context.strokeStyle = control.iconColor;
            context.lineWidth = 1.3;
            context.lineCap = "round";
            context.lineJoin = "round";
            context.beginPath();
            if (control.kind === "up") {
                context.moveTo(8, 13);
                context.lineTo(8, 3.5);
                context.moveTo(4, 7.5);
                context.lineTo(8, 3.5);
                context.lineTo(12, 7.5);
            } else if (control.kind === "folder") {
                context.moveTo(2, 12.5);
                context.lineTo(2, 3.5);
                context.lineTo(6, 3.5);
                context.lineTo(7.8, 5.5);
                context.lineTo(14, 5.5);
                context.lineTo(14, 12.5);
                context.closePath();
            } else {
                context.moveTo(3.5, 2);
                context.lineTo(9.5, 2);
                context.lineTo(12.5, 5);
                context.lineTo(12.5, 14);
                context.lineTo(3.5, 14);
                context.closePath();
                context.moveTo(9.5, 2);
                context.lineTo(9.5, 5);
                context.lineTo(12.5, 5);
            }
            context.stroke();
        }

        Connections {
            target: control
            function onIconColorChanged() { iconCanvas.requestPaint(); }
            function onKindChanged() { iconCanvas.requestPaint(); }
        }
    }
}
