import QtQuick
import QtQuick.Controls
import Qt.labs.folderlistmodel
import "ThemeColors.js" as ThemeColors

// An in-app replacement for the platform file dialog, so opening and saving
// look like the rest of the editor instead of borrowing whichever toolkit the
// desktop portal happens to answer with.
Dialog {
    id: root

    property bool saving: false
    property url folder
    property string suggestedName: "Untitled.md"
    property bool showHidden: false
    property bool markdownOnly: true

    property bool darkMode: true
    property color pageColor: darkMode ? "#101010" : "#ffffff"
    property color inkColor: darkMode ? "#eeeeee" : "#222324"
    property color mutedColor: ThemeColors.mix(pageColor, inkColor, 0.45)
    property color surfaceColor: ThemeColors.mix(pageColor, inkColor, 0.08)
    property color accentColor: "#428bca"
    property color selectionColor: ThemeColors.mix(accentColor, pageColor, 0.45)
    property int containerWidth: 900
    property int containerHeight: 640
    property real textScale: 1

    signal fileChosen(url file)
    signal canceled()

    readonly property string mono: "iA Writer Mono S"
    readonly property var crumbs: backend.folderCrumbs(folder)

    function sized(value) { return Math.round(value * textScale); }

    function openIn(startFolder) {
        folder = startFolder;
        fileList.currentIndex = -1;
        pendingOverwrite = "";
        if (saving) {
            nameField.text = suggestedName;
            saveFormatIndex = suffixIndexFor(suggestedName);
        }
        open();
        if (saving)
            nameField.forceActiveFocus();
        else
            fileList.forceActiveFocus();
    }

    // The chosen file: whatever is typed when saving, otherwise the selected row.
    function chosenFile() {
        if (saving) {
            var name = nameField.text.trim();
            if (name.length === 0)
                return "";
            if (!/\.[^./]+$/.test(name))
                name += root.saveSuffix;
            return backend.folderChild(root.folder, name);
        }
        if (fileList.currentIndex < 0)
            return "";
        if (folderModel.isFolder(fileList.currentIndex))
            return "";
        return folderModel.get(fileList.currentIndex, "fileUrl");
    }

    // Name of the file a Save is about to replace, empty when nothing is pending.
    property string pendingOverwrite: ""

    // Save format. The extension is what actually decides the file type, so the
    // toggle just rewrites it on the name rather than tracking a separate state.
    readonly property var saveFormats: [
        { label: "Markdown  .md", suffix: ".md" },
        { label: "Plain text  .txt", suffix: ".txt" }
    ]
    property int saveFormatIndex: 0
    readonly property string saveSuffix: saveFormats[saveFormatIndex].suffix

    // Extensions the toggle is allowed to replace. Anything else the user typed
    // deliberately (.markdown, .conf, .org) is left alone.
    readonly property var knownSuffixes: /\.(md|markdown|mdown|mkd|txt|text)$/i

    // The toggles below swap their own caption, and a caption of a different
    // length would resize the button and shove everything after it along the
    // row. Size them to their widest caption instead.
    function widestLabel(labels) {
        var widest = "";
        for (var i = 0; i < labels.length; ++i) {
            if (labels[i].length > widest.length)
                widest = labels[i];
        }
        return widest;
    }

    function suffixIndexFor(name) {
        return /\.(txt|text)$/i.test(name) ? 1 : 0;
    }

    function cycleSaveFormat() {
        saveFormatIndex = (saveFormatIndex + 1) % saveFormats.length;

        var name = nameField.text.trim();
        if (name.length === 0)
            return;
        if (knownSuffixes.test(name))
            name = name.replace(knownSuffixes, "");
        nameField.text = name + saveSuffix;
    }

    // Everything that confirms the dialog goes through here so Save cannot
    // silently replace a file. The platform dialog used to ask on our behalf.
    function submit() {
        var file = chosenFile();
        if (file.toString().length === 0)
            return;

        var replacing = root.saving
            && backend.fileExists(file)
            && file.toString() !== backend.fileUrl.toString();
        if (replacing) {
            root.pendingOverwrite = backend.fileNameOf(file);
            return;
        }

        root.accept();
    }

    function activateRow(index) {
        if (index < 0)
            return;
        if (folderModel.isFolder(index)) {
            root.folder = folderModel.get(index, "fileUrl");
            fileList.currentIndex = -1;
            return;
        }
        if (saving) {
            nameField.text = folderModel.get(index, "fileName");
            saveFormatIndex = suffixIndexFor(nameField.text);
        } else {
            root.submit();
        }
    }

    function goUp() {
        var up = backend.parentFolder(root.folder);
        if (up.toString().length === 0)
            return;
        root.folder = up;
        fileList.currentIndex = -1;
    }

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    // The Material style sets topPadding: 16 *after* padding: 24, so a plain
    // "padding: 0" leaves a strip of dialog background above the content.
    padding: 0
    topPadding: 0
    bottomPadding: 0
    // The Material style ships an empty header and a DialogButtonBox footer;
    // both would reserve space above and below the browser's own chrome.
    header: null
    footer: null

    // Material's Dialog style re-declares Overlay.modal with its own dim colour,
    // which in the dark theme is a *light* wash that turns the page grey. Setting
    // it per dialog is the only way to win: a window-level override is ignored.
    Overlay.modal: Rectangle {
        color: Qt.rgba(root.pageColor.r, root.pageColor.g, root.pageColor.b, 0.72)
    }
    width: Math.min(sized(880), containerWidth - 48)
    height: Math.min(sized(600), containerHeight - 48)
    x: Math.round((containerWidth - width) / 2)
    y: Math.round((containerHeight - height) / 2)

    onRejected: root.canceled()
    onAccepted: {
        var file = chosenFile();
        if (file === "")
            return;
        root.fileChosen(file);
    }

    background: Rectangle {
        color: root.pageColor
        border.color: ThemeColors.mix(root.pageColor, root.inkColor, 0.22)
        radius: 0
    }

    FolderListModel {
        id: folderModel
        folder: root.folder
        showDirs: true
        showDirsFirst: true
        showDotAndDotDot: false
        showHidden: root.showHidden
        sortField: FolderListModel.Name
        caseSensitive: false
        nameFilters: root.markdownOnly && !root.saving
            ? ["*.md", "*.markdown", "*.mdown", "*.mkd", "*.txt"]
            : ["*"]
    }

    contentItem: Item {
        implicitWidth: root.sized(880)
        implicitHeight: root.sized(600)

        // ---- Breadcrumb bar -------------------------------------------------
        Rectangle {
            id: crumbBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: root.sized(44)
            color: root.surfaceColor

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Item {
                    width: root.sized(24)
                    height: root.sized(24)
                    anchors.verticalCenter: parent.verticalCenter

                    FileKindIcon {
                        anchors.centerIn: parent
                        kind: "up"
                        opacity: upArea.enabled ? 1 : 0.3
                        iconColor: upArea.containsMouse ? root.inkColor : root.mutedColor
                    }

                    MouseArea {
                        id: upArea
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: backend.parentFolder(root.folder).toString().length > 0
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.goUp()
                    }
                }

                Label {
                    text: root.saving ? "Save to" : "Open from"
                    color: root.mutedColor
                    font.family: root.mono
                    font.pixelSize: root.sized(12)
                    leftPadding: 6
                    rightPadding: 10
                    anchors.verticalCenter: parent.verticalCenter
                }

                Repeater {
                    model: root.crumbs

                    Row {
                        spacing: 4
                        anchors.verticalCenter: parent.verticalCenter

                        Label {
                            visible: index > 0
                            text: "/"
                            color: root.mutedColor
                            font.family: root.mono
                            font.pixelSize: root.sized(13)
                        }

                        Label {
                            text: modelData.name
                            color: index === root.crumbs.length - 1 ? root.inkColor : root.mutedColor
                            font.family: root.mono
                            font.pixelSize: root.sized(13)

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -3
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.folder = modelData.url;
                                    fileList.currentIndex = -1;
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- Places sidebar -------------------------------------------------
        Rectangle {
            id: sidebar
            anchors.left: parent.left
            anchors.top: crumbBar.bottom
            anchors.bottom: footer.top
            visible: root.width >= root.sized(520)
            width: visible ? root.sized(160) : 0
            color: root.surfaceColor

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 8

                Repeater {
                    model: backend.standardPlaces()

                    Rectangle {
                        width: sidebar.width
                        height: root.sized(32)
                        color: placeArea.containsMouse
                            ? ThemeColors.mix(root.surfaceColor, root.inkColor, 0.1)
                            : "transparent"

                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            color: root.folder.toString() === modelData.url.toString()
                                ? root.inkColor : root.mutedColor
                            font.family: root.mono
                            font.pixelSize: root.sized(13)
                        }

                        MouseArea {
                            id: placeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.folder = modelData.url;
                                fileList.currentIndex = -1;
                            }
                        }
                    }
                }
            }
        }

        // ---- File list ------------------------------------------------------
        ListView {
            id: fileList
            anchors.left: sidebar.right
            anchors.right: parent.right
            anchors.top: crumbBar.bottom
            anchors.bottom: footer.top
            clip: true
            currentIndex: -1
            model: folderModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                contentItem: Rectangle {
                    implicitWidth: 4
                    radius: 2
                    color: ThemeColors.mix(root.pageColor, root.inkColor, 0.28)
                }
            }

            Keys.onReturnPressed: function(event) {
                root.activateRow(currentIndex);
                event.accepted = true;
            }
            Keys.onEnterPressed: function(event) {
                root.activateRow(currentIndex);
                event.accepted = true;
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Backspace) {
                    root.goUp();
                    event.accepted = true;
                }
            }

            Label {
                anchors.centerIn: parent
                visible: folderModel.count === 0
                text: root.markdownOnly && !root.saving
                    ? "No documents here" : "Empty folder"
                color: root.mutedColor
                font.family: root.mono
                font.pixelSize: root.sized(13)
            }

            delegate: Rectangle {
                width: fileList.width
                height: root.sized(34)
                color: fileList.currentIndex === index
                    ? root.selectionColor
                    : rowArea.containsMouse
                        ? ThemeColors.mix(root.pageColor, root.inkColor, 0.07)
                        : "transparent"

                FileKindIcon {
                    id: kindIcon
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    kind: fileIsDir ? "folder" : "file"
                    iconColor: fileList.currentIndex === index ? root.inkColor : root.mutedColor
                }

                Label {
                    anchors.left: kindIcon.right
                    anchors.leftMargin: 12
                    anchors.right: detail.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: fileName
                    elide: Text.ElideMiddle
                    color: root.inkColor
                    font.family: root.mono
                    font.pixelSize: root.sized(14)
                }

                Row {
                    id: detail
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 16

                    Label {
                        text: fileIsDir ? "" : root.humanSize(fileSize)
                        horizontalAlignment: Text.AlignRight
                        width: root.sized(64)
                        color: root.mutedColor
                        font.family: root.mono
                        font.pixelSize: root.sized(12)
                    }

                    Label {
                        text: Qt.formatDateTime(fileModified, "d MMM yyyy")
                        horizontalAlignment: Text.AlignRight
                        width: root.sized(90)
                        color: root.mutedColor
                        font.family: root.mono
                        font.pixelSize: root.sized(12)
                    }
                }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        fileList.currentIndex = index;
                        fileList.forceActiveFocus();
                        if (root.saving && !fileIsDir) {
                            nameField.text = fileName;
                            root.saveFormatIndex = root.suffixIndexFor(fileName);
                        }
                    }
                    onDoubleClicked: root.activateRow(index)
                }
            }
        }

        // ---- Overwrite confirmation -----------------------------------------
        Rectangle {
            anchors.fill: parent
            visible: root.pendingOverwrite.length > 0
            color: Qt.rgba(root.pageColor.r, root.pageColor.g, root.pageColor.b, 0.94)
            z: 20

            // Swallow anything aimed at the browser underneath.
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
            }

            Keys.onEscapePressed: function(event) {
                root.pendingOverwrite = "";
                event.accepted = true;
            }

            Column {
                anchors.centerIn: parent
                width: Math.min(root.sized(420), parent.width - root.sized(48))
                spacing: root.sized(14)

                Label {
                    text: "Replace " + root.pendingOverwrite + "?"
                    width: parent.width
                    elide: Text.ElideMiddle
                    color: root.inkColor
                    font.family: root.mono
                    font.pixelSize: root.sized(16)
                    font.bold: true
                }

                Label {
                    text: "A file with that name already exists in this folder. "
                        + "Its contents will be overwritten."
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: root.mutedColor
                    font.family: root.mono
                    font.pixelSize: root.sized(13)
                }

                Row {
                    anchors.right: parent.right
                    spacing: 8

                    SquareDialogButton {
                        text: "Cancel"
                        darkMode: root.darkMode
                        pageColor: root.pageColor
                        inkColor: root.inkColor
                        textScale: root.textScale
                        onClicked: root.pendingOverwrite = ""
                    }

                    SquareDialogButton {
                        id: replaceButton
                        text: "Replace"
                        primary: true
                        darkMode: root.darkMode
                        pageColor: root.pageColor
                        inkColor: root.inkColor
                        activeColor: root.accentColor
                        textScale: root.textScale
                        onClicked: {
                            root.pendingOverwrite = "";
                            root.accept();
                        }
                    }
                }
            }
        }

        // ---- Footer ---------------------------------------------------------
        Rectangle {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.sized(60)
            color: root.surfaceColor

            // Matches SquareDialogButton's own label font, so the measurement
            // lines up with what the button will actually render.
            TextMetrics {
                id: formatMetrics
                font.family: root.mono
                font.pixelSize: Math.round(12 * root.textScale)
                text: root.widestLabel(root.saveFormats.map(function (f) { return f.label; }))
            }

            TextMetrics {
                id: filterMetrics
                font.family: root.mono
                font.pixelSize: Math.round(12 * root.textScale)
                text: root.widestLabel(["All files", "Documents only"])
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Label {
                    visible: root.saving
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Name"
                    color: root.mutedColor
                    font.family: root.mono
                    font.pixelSize: root.sized(13)
                }

                Rectangle {
                    visible: root.saving
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(root.sized(150), footer.width - root.sized(560))
                    height: root.sized(32)
                    color: root.pageColor
                    border.color: nameField.activeFocus
                        ? root.inkColor
                        : ThemeColors.mix(root.surfaceColor, root.inkColor, 0.25)

                    TextInput {
                        id: nameField
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        clip: true
                        color: root.inkColor
                        selectionColor: root.selectionColor
                        selectedTextColor: root.inkColor
                        font.family: root.mono
                        font.pixelSize: root.sized(14)
                        Keys.onReturnPressed: root.submit()
                        KeyNavigation.tab: confirmButton
                    }
                }

                // The extension decides the file type, so this stays visible
                // even when the narrow layout drops the other toggles.
                SquareDialogButton {
                    visible: root.saving
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(88, Math.ceil(formatMetrics.width) + 32)
                    text: root.saveFormats[root.saveFormatIndex].label
                    darkMode: root.darkMode
                    pageColor: root.surfaceColor
                    inkColor: root.inkColor
                    textScale: root.textScale
                    onClicked: root.cycleSaveFormat()
                }

                // Hidden files and the Markdown filter, as plain toggles.
                SquareDialogButton {
                    visible: root.width >= root.sized(600)
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.showHidden ? "Hide dotfiles" : "Show dotfiles"
                    darkMode: root.darkMode
                    pageColor: root.surfaceColor
                    inkColor: root.inkColor
                    textScale: root.textScale
                    onClicked: root.showHidden = !root.showHidden
                }

                SquareDialogButton {
                    visible: !root.saving && root.width >= root.sized(600)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(88, Math.ceil(filterMetrics.width) + 32)
                    text: root.markdownOnly ? "All files" : "Documents only"
                    darkMode: root.darkMode
                    pageColor: root.surfaceColor
                    inkColor: root.inkColor
                    textScale: root.textScale
                    onClicked: root.markdownOnly = !root.markdownOnly
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                SquareDialogButton {
                    text: "Cancel"
                    darkMode: root.darkMode
                    pageColor: root.surfaceColor
                    inkColor: root.inkColor
                    textScale: root.textScale
                    onClicked: root.reject()
                }

                SquareDialogButton {
                    id: confirmButton
                    text: root.saving ? "Save" : "Open"
                    primary: true
                    enabled: root.chosenFile() !== ""
                    darkMode: root.darkMode
                    pageColor: root.surfaceColor
                    inkColor: root.inkColor
                    activeColor: root.accentColor
                    textScale: root.textScale
                    onClicked: root.submit()
                }
            }
        }
    }

    function humanSize(bytes) {
        if (bytes < 1024)
            return bytes + " B";
        var units = ["KiB", "MiB", "GiB", "TiB"];
        var value = bytes / 1024;
        var unit = 0;
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024;
            unit++;
        }
        return (value < 10 ? value.toFixed(1) : Math.round(value)) + " " + units[unit];
    }
}
