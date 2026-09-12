#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>

#include <QColor>

#include "backend.h"
#include "markdownhighlighter.h"

namespace {
// Point QDir::homePath() at a scratch directory for the duration of a test.
class HomeOverride {
public:
    explicit HomeOverride(const QString &path) : m_original(qgetenv("HOME")) {
        qputenv("HOME", path.toUtf8());
    }
    ~HomeOverride() { qputenv("HOME", m_original); }

private:
    QByteArray m_original;
};
}

class WwriteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void loadsPywalPalette() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());
        const HomeOverride home(homeDirectory.path());

        const QString walDirectory = homeDirectory.path() + QStringLiteral("/.cache/wal");
        QVERIFY(QDir().mkpath(walDirectory));

        QFile colorsFile(walDirectory + QStringLiteral("/colors.json"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "{\n"
            "  \"special\": { \"background\": \"#fefefe\", \"foreground\": \"#101010\" },\n"
            "  \"colors\": {\n"
            "    \"color0\": \"#fefefe\", \"color1\": \"#fdfdfd\",\n"
            "    \"color4\": \"#112233\", \"color8\": \"#808080\"\n"
            "  }\n"
            "}\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        // color4 is the first entry pywal templates reach for as an accent.
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        // color8 is pywal's dim grey, and reads fine against this background.
        QCOMPARE(backend.themeMuted(), QStringLiteral("#808080"));
        // A light wallpaper has to put the app in light mode.
        QVERIFY(!backend.darkMode());

        // The derived colours are blends, so assert what they have to satisfy
        // rather than pinning exact rounding.
        QVERIFY(QColor(backend.themeSelection()).isValid());
        QVERIFY(backend.themeSelection() != backend.themeBackground());
        QVERIFY(QColor(backend.themeSurface()).isValid());
        QVERIFY(backend.themeSurface() != backend.themeBackground());
    }

    void fallsBackToPlainPywalColors() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());
        const HomeOverride home(homeDirectory.path());

        const QString walDirectory = homeDirectory.path() + QStringLiteral("/.cache/wal");
        QVERIFY(QDir().mkpath(walDirectory));

        // No colors.json: the plain `colors` file has to carry the palette,
        // with color0 as the page and color7 as the ink.
        QFile colorsFile(walDirectory + QStringLiteral("/colors"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "#101114\n#803030\n#308030\n#808030\n#5f87af\n#803080\n#308080\n#d0d0d0\n"
            "#606060\n#803030\n#308030\n#808030\n#5f87af\n#803080\n#308080\n#d0d0d0\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#101114"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#d0d0d0"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#5f87af"));
        QCOMPARE(backend.themeMuted(), QStringLiteral("#606060"));
        QVERIFY(backend.darkMode());
    }

    void rethemesWhenPywalPaletteChanges() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());
        const HomeOverride home(homeDirectory.path());

        const QString walDirectory = homeDirectory.path() + QStringLiteral("/.cache/wal");
        QVERIFY(QDir().mkpath(walDirectory));

        const QString colorsPath = walDirectory + QStringLiteral("/colors.json");
        const auto writePalette = [&colorsPath](const QByteArray &contents) {
            QFile file(colorsPath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                return false;
            const bool written = file.write(contents) == contents.size();
            file.close();
            return written;
        };

        QVERIFY(writePalette(
            "{\"special\": {\"background\": \"#101114\", \"foreground\": \"#d0d0d0\"},"
            " \"colors\": {\"color4\": \"#5f87af\"}}"));

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#101114"));
        QVERIFY(backend.darkMode());

        QSignalSpy themeSpy(&backend, &Backend::themeColorsChanged);

        // Stand in for `wal -i some-light-wallpaper.jpg`.
        QVERIFY(writePalette(
            "{\"special\": {\"background\": \"#fdf6e3\", \"foreground\": \"#3b4252\"},"
            " \"colors\": {\"color4\": \"#268bd2\"}}"));

        QVERIFY(themeSpy.wait(5000));
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fdf6e3"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#3b4252"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#268bd2"));
        // A light wallpaper has to take the app out of dark mode with it.
        QVERIFY(!backend.darkMode());
    }

    void keepsBuiltInPaletteWithoutPywal() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());
        const HomeOverride home(homeDirectory.path());

        Backend backend;
        QVERIFY(QColor(backend.themeBackground()).isValid());
        QVERIFY(QColor(backend.themeForeground()).isValid());
        QVERIFY(QColor(backend.themeAccent()).isValid());
        QVERIFY(QColor(backend.themeMuted()).isValid());

        // With no palette to read, the desktop portal still gets to flip modes.
        const bool wasDark = backend.darkMode();
        backend.setDarkMode(!wasDark);
        QCOMPARE(backend.darkMode(), !wasDark);
    }

    void walksAboveTheHomeFolder() {
        Backend backend;

        // $HOME collapses to a single "~" crumb, so the browser cannot reach its
        // parent through the breadcrumbs; parentFolder has to do it.
        const QUrl home = QUrl::fromLocalFile(QDir::homePath());
        const QUrl aboveHome = backend.parentFolder(home);
        QVERIFY(aboveHome.isValid());
        QCOMPARE(aboveHome.toLocalFile(),
                 QFileInfo(QDir::homePath()).absolutePath());

        // Walking up repeatedly has to terminate at the filesystem root.
        QUrl current = home;
        for (int step = 0; step < 64; ++step) {
            const QUrl next = backend.parentFolder(current);
            if (next.toString().isEmpty())
                break;
            current = next;
        }
        QCOMPARE(current.toLocalFile(), QStringLiteral("/"));
        QVERIFY(backend.parentFolder(QUrl::fromLocalFile(QStringLiteral("/")))
                    .toString().isEmpty());
    }

    void alwaysStartsBrowsingInTheHomeFolder() {
        Backend backend;
        const QString home = QDir::homePath();

        QCOMPARE(backend.startFolder().toLocalFile(), home);

        // A previous excursion outside $HOME must not become the new default.
        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             QStringLiteral("/etc"));
        QCOMPARE(backend.startFolder().toLocalFile(), home);

        // Nor does having a document open somewhere else.
        QTemporaryDir elsewhere;
        QVERIFY(elsewhere.isValid());
        backend.saveAs(QUrl::fromLocalFile(elsewhere.filePath(QStringLiteral("a.md"))));
        QCOMPARE(backend.startFolder().toLocalFile(), home);
    }

    void saveAsStartsBesideTheDocument() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        Backend backend;
        // Nothing saved yet: home, like every other dialog.
        QCOMPARE(backend.saveStartFolder().toLocalFile(), QDir::homePath());

        backend.saveAs(QUrl::fromLocalFile(directory.filePath(QStringLiteral("note.md"))));
        // Now Save As should offer the folder the document actually lives in.
        QCOMPARE(backend.saveStartFolder().toLocalFile(),
                 QFileInfo(directory.filePath(QStringLiteral("note.md"))).absolutePath());

        // Open is unaffected and still starts at home.
        QCOMPARE(backend.startFolder().toLocalFile(), QDir::homePath());
    }

    void refusesToOpenThingsThatAreNotDocuments() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Backend backend;

        // A directory.
        backend.open(QUrl::fromLocalFile(directory.path()));
        QVERIFY(backend.status().contains(QStringLiteral("folder")));
        QVERIFY(backend.fileUrl().isEmpty());

        // A binary file: NUL bytes would become replacement characters and be
        // written back as corruption on the next save.
        const QString binaryPath = directory.filePath(QStringLiteral("blob.md"));
        QFile binary(binaryPath);
        QVERIFY(binary.open(QIODevice::WriteOnly));
        binary.write(QByteArray("PK\x03\x04", 4) + QByteArray(16, '\0'));
        binary.close();
        backend.open(QUrl::fromLocalFile(binaryPath));
        QVERIFY(backend.status().contains(QStringLiteral("not a text file")));
        QVERIFY(backend.fileUrl().isEmpty());

        // A character device would otherwise never finish reading.
        if (QFileInfo::exists(QStringLiteral("/dev/zero"))) {
            backend.open(QUrl::fromLocalFile(QStringLiteral("/dev/zero")));
            QVERIFY(backend.status().contains(QStringLiteral("not a regular file")));
            QVERIFY(backend.fileUrl().isEmpty());
        }

        // A perfectly ordinary document still opens.
        const QString goodPath = directory.filePath(QStringLiteral("fine.md"));
        QFile good(goodPath);
        QVERIFY(good.open(QIODevice::WriteOnly | QIODevice::Text));
        good.write("# Fine\n");
        good.close();
        backend.open(QUrl::fromLocalFile(goodPath));
        QCOMPARE(backend.fileUrl().toLocalFile(), goodPath);
    }

    void reportsWhenAFileAlreadyExists() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Backend backend;

        const QUrl target =
            QUrl::fromLocalFile(directory.filePath(QStringLiteral("taken.md")));
        QVERIFY(!backend.fileExists(target));

        backend.saveAs(target);
        // The browser uses this to decide whether Save needs to ask first.
        QVERIFY(backend.fileExists(target));
        QVERIFY(!backend.fileExists(
            QUrl::fromLocalFile(directory.filePath(QStringLiteral("free.md")))));
    }

    void buildsCrumbsOutsideHome() {
        Backend backend;

        const QVariantList crumbs =
            backend.folderCrumbs(QUrl::fromLocalFile(QStringLiteral("/etc")));
        QCOMPARE(crumbs.size(), 2);
        QCOMPARE(crumbs.at(0).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("/"));
        QCOMPARE(crumbs.at(1).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("etc"));
        QCOMPARE(crumbs.at(1).toMap().value(QStringLiteral("url")).toUrl().toLocalFile(),
                 QStringLiteral("/etc"));

        // The sidebar has to offer a way out of $HOME in the first place.
        const QVariantList places = backend.standardPlaces();
        bool hasRoot = false;
        for (const QVariant &place : places) {
            if (place.toMap().value(QStringLiteral("url")).toUrl().toLocalFile()
                    == QStringLiteral("/"))
                hasRoot = true;
        }
        QVERIFY(hasRoot);
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void offersSaveAsFromTheFooter() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *saveAsButton =
            window->findChild<QObject *>(QStringLiteral("saveAsButton"));
        QVERIFY(saveAsButton);

        // An already-saved document: Save As must still offer a dialog rather
        // than writing straight over the open file the way plain Save does.
        const QString path = directory.filePath(QStringLiteral("kept.md"));
        backend.saveAs(QUrl::fromLocalFile(path));
        QCOMPARE(backend.fileUrl().toLocalFile(), path);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveAsButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        // And it opens beside the document, not at home.
        QCOMPARE(backend.saveStartFolder().toLocalFile(),
                 QFileInfo(path).absolutePath());
    }

    void savesPlainTextAsWellAsMarkdown() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Backend backend;

        const QString textPath = directory.filePath(QStringLiteral("notes.txt"));
        backend.saveAs(QUrl::fromLocalFile(textPath));
        QVERIFY(QFileInfo::exists(textPath));
        QCOMPARE(backend.fileName(), QStringLiteral("notes.txt"));

        // A .txt document reopens as text, and Save As keeps its extension
        // rather than being forced back to Markdown.
        backend.open(QUrl::fromLocalFile(textPath));
        QCOMPARE(backend.fileName(), QStringLiteral("notes.txt"));

        // The suggested name for a brand new document is still Markdown.
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("a heading")),
                 QStringLiteral("a heading.md"));
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // A 16px desktop text size sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(WwriteTest)
#include "tst_wwrite.moc"
