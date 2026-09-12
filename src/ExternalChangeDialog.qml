import QtQuick
import QtQuick.Controls
import "ThemeColors.js" as ThemeColors

Dialog {
    id: root

    property bool deleted: false
    property bool locallyModified: false
    property bool darkMode: true
    property color pageColor: darkMode ? "#101010" : "#ffffff"
    property color textColor: darkMode ? "#d0d0d0" : "#42464c"
    property color strongTextColor: darkMode ? "#eeeeee" : "#222324"
    property color activeButtonColor: "#428bca"
    property color surfaceColor: ThemeColors.mix(pageColor, strongTextColor, 0.08)
    property int containerWidth: 520
    property int containerHeight: 320
    property real textScale: 1

    signal keepRequested()
    signal reloadRequested()

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    // Dim toward the page colour; Material's own dim is a light wash in the
    // dark theme, and it overrides anything set at the window level.
    Overlay.modal: Rectangle {
        color: Qt.rgba(root.pageColor.r, root.pageColor.g, root.pageColor.b, 0.72)
    }
    width: Math.min(520, containerWidth - 48)
    x: Math.round((containerWidth - width) / 2)
    y: Math.round((containerHeight - height) / 2)
    padding: 20
    topPadding: 20

    onOpened: (deleted ? keepButton : reloadButton).forceActiveFocus()

    background: Rectangle {
        color: root.surfaceColor
        border.color: ThemeColors.mix(root.surfaceColor, root.strongTextColor, 0.22)
        radius: 0
    }

    contentItem: Column {
        spacing: 12

        Label {
            text: root.deleted ? "File removed" : "File changed"
            color: root.strongTextColor
            font.family: "iA Writer Mono S"
            font.pixelSize: Math.round(16 * root.textScale)
            font.bold: true
        }

        Label {
            width: parent.width
            text: root.deleted
                ? "This file was removed outside Notes. Keep your text as an unsaved document?"
                : (root.locallyModified
                   ? "This file changed outside Notes. Reloading will discard your changes."
                   : "This file changed outside Notes.")
            color: root.textColor
            wrapMode: Text.Wrap
            font.family: "iA Writer Mono S"
            font.pixelSize: Math.round(13 * root.textScale)
        }
    }

    footer: Item {
        implicitHeight: dialogButtons.implicitHeight + 20

        Row {
            id: dialogButtons
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            SquareDialogButton {
                id: keepButton
                text: "Keep Mine"
                darkMode: root.darkMode
                pageColor: root.surfaceColor
                inkColor: root.strongTextColor
                textScale: root.textScale
                labelColor: root.deleted
                    ? ThemeColors.readableOn(root.activeButtonColor,
                                             root.surfaceColor, root.strongTextColor)
                    : root.textColor
                primary: root.deleted
                activeColor: root.activeButtonColor
                KeyNavigation.left: reloadButton
                KeyNavigation.right: reloadButton
                KeyNavigation.tab: reloadButton
                KeyNavigation.backtab: reloadButton
                onClicked: {
                    root.close();
                    root.keepRequested();
                }
            }

            SquareDialogButton {
                id: reloadButton
                text: "Reload"
                enabled: !root.deleted
                primary: true
                darkMode: root.darkMode
                pageColor: root.surfaceColor
                inkColor: root.strongTextColor
                textScale: root.textScale
                activeColor: root.activeButtonColor
                KeyNavigation.left: keepButton
                KeyNavigation.right: keepButton
                KeyNavigation.tab: keepButton
                KeyNavigation.backtab: keepButton
                onClicked: {
                    root.close();
                    root.reloadRequested();
                }
            }
        }
    }
}
