import QtQuick
import QtQuick.Controls
import "ThemeColors.js" as ThemeColors

Dialog {
    id: root

    property string fileName: "Untitled.md"
    property bool darkMode: true
    property color pageColor: darkMode ? "#101010" : "#ffffff"
    property color textColor: darkMode ? "#d0d0d0" : "#42464c"
    property color strongTextColor: darkMode ? "#eeeeee" : "#222324"
    property color activeButtonColor: "#428bca"
    property color surfaceColor: ThemeColors.mix(pageColor, strongTextColor, 0.08)
    property int containerWidth: 420
    property int containerHeight: 320
    property real textScale: 1

    signal saveRequested()
    signal discardRequested()
    signal cancelRequested()

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    // Dim toward the page colour; Material's own dim is a light wash in the
    // dark theme, and it overrides anything set at the window level.
    Overlay.modal: Rectangle {
        color: Qt.rgba(root.pageColor.r, root.pageColor.g, root.pageColor.b, 0.72)
    }
    onRejected: cancelRequested()

    onOpened: saveButton.forceActiveFocus()
    width: Math.min(420, containerWidth - 48)
    x: Math.round((containerWidth - width) / 2)
    y: Math.round((containerHeight - height) / 2)
    padding: 20
    topPadding: 20

    background: Rectangle {
        color: root.surfaceColor
        border.color: ThemeColors.mix(root.surfaceColor, root.strongTextColor, 0.22)
        radius: 0
    }

    contentItem: Column {
        spacing: 12

        Label {
            text: "Unsaved changes"
            color: root.strongTextColor
            font.family: "iA Writer Mono S"
            font.pixelSize: Math.round(16 * root.textScale)
            font.bold: true
        }

        Label {
            width: parent.width
            text: "Save changes to " + root.fileName + " before closing?"
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
                id: cancelButton
                text: "Cancel"
                darkMode: root.darkMode
                pageColor: root.surfaceColor
                inkColor: root.strongTextColor
                textScale: root.textScale
                labelColor: root.textColor
                KeyNavigation.left: saveButton
                KeyNavigation.right: discardButton
                KeyNavigation.tab: discardButton
                KeyNavigation.backtab: saveButton
                onClicked: root.reject()
            }

            SquareDialogButton {
                id: discardButton
                text: "Discard"
                darkMode: root.darkMode
                pageColor: root.surfaceColor
                inkColor: root.strongTextColor
                textScale: root.textScale
                labelColor: root.textColor
                KeyNavigation.left: cancelButton
                KeyNavigation.right: saveButton
                KeyNavigation.tab: saveButton
                KeyNavigation.backtab: cancelButton
                onClicked: {
                    root.close();
                    root.discardRequested();
                }
            }

            SquareDialogButton {
                id: saveButton
                text: "Save"
                primary: true
                darkMode: root.darkMode
                pageColor: root.surfaceColor
                inkColor: root.strongTextColor
                textScale: root.textScale
                activeColor: root.activeButtonColor
                KeyNavigation.left: discardButton
                KeyNavigation.right: cancelButton
                KeyNavigation.tab: cancelButton
                KeyNavigation.backtab: discardButton
                onClicked: {
                    root.close();
                    root.saveRequested();
                }
            }
        }
    }
}
