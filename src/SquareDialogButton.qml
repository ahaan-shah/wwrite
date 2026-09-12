import QtQuick
import QtQuick.Controls
import "ThemeColors.js" as ThemeColors

Button {
    id: control

    property bool primary: false
    property bool darkMode: true
    property color pageColor: darkMode ? "#101010" : "#ffffff"
    property color inkColor: darkMode ? "#eeeeee" : "#222324"
    property color surfaceColor: ThemeColors.mix(pageColor, inkColor, 0.08)
    property color activeColor: "#428bca"
    property color labelColor: primary
        ? ThemeColors.readableOn(activeColor, pageColor, inkColor)
        : inkColor
    property real textScale: 1

    readonly property color restColor: primary ? activeColor : surfaceColor
    readonly property color hoverColor: ThemeColors.mix(restColor, inkColor, 0.12)
    readonly property color pressColor: ThemeColors.mix(restColor, pageColor, 0.22)

    opacity: control.enabled ? 1 : 0.4

    leftPadding: 16
    rightPadding: 16
    topPadding: 7
    bottomPadding: 7

    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    contentItem: Label {
        text: control.text
        color: control.labelColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: "iA Writer Mono S"
        font.pixelSize: Math.round(12 * control.textScale)
    }

    background: Rectangle {
        implicitWidth: 88
        implicitHeight: 34
        radius: 0
        color: control.down
            ? control.pressColor
            : control.hovered ? control.hoverColor : control.restColor
        border.color: control.activeFocus
            ? control.inkColor
            : ThemeColors.mix(control.restColor, control.inkColor, 0.3)
    }
}
