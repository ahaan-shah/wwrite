#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMimeData>
#include <QPrintDialog>
#include <QPrinter>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>

#include "markdownhighlighter.h"

constexpr qreal typoraLineHeightPercent = 140;
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");

// The file browser now reaches the whole filesystem, so open() has to survive
// being pointed at things that are not documents. 64 MiB is far past any
// plausible piece of writing and still loads without stalling the UI.
constexpr qint64 maximumOpenSize = 64 * 1024 * 1024;

QString Backend::normalizedLinkUrl(const QString &clipboardText) {
    QString candidate = clipboardText.trimmed();
    static const QRegularExpression lineBreakRe(QStringLiteral("[\\r\\n]"));
    const int lineBreak = candidate.indexOf(lineBreakRe);
    if (lineBreak >= 0)
        candidate = candidate.left(lineBreak).trimmed();

    if (candidate.isEmpty())
        return {};

    if (candidate.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        candidate.prepend(QStringLiteral("https://"));

    static const QRegularExpression schemeRe(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (!schemeRe.match(candidate).hasMatch())
        return {};

    const QUrl url(candidate);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};

    const QString scheme = url.scheme().toLower();
    const bool webUrl = scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("ftp");
    if (webUrl && url.host().isEmpty())
        return {};

    if (!webUrl && scheme != QStringLiteral("mailto"))
        return {};

    return url.toString();
}

Backend::Backend(QObject *parent, bool recoverOrphans)
    : QObject(parent), m_recoverOrphans(recoverOrphans) {
    const QString stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(stateDirectory);
    // Claim an orphaned snapshot before taking an empty slot. This ensures a
    // crash in window 2 is still recovered even if window 1 exited normally.
    for (int pass = recoverOrphans ? 0 : 1; pass < 2 && !m_recoveryLock; ++pass) {
        for (int slot = 0; slot < 100; ++slot) {
            const QString base = QDir(stateDirectory).filePath(
                QStringLiteral("recovery-%1").arg(slot));
            const bool snapshotExists = QFileInfo::exists(base + QStringLiteral(".json"));
            if ((pass == 0) != snapshotExists)
                continue;
            auto lock = std::make_unique<QLockFile>(base + QStringLiteral(".lock"));
            if (lock->tryLock()) {
                m_recoveryPath = base + QStringLiteral(".json");
                m_recoveryLock = std::move(lock);
                break;
            }
        }
    }
    m_wordCountTimer.setSingleShot(true);
    m_wordCountTimer.setInterval(120);
    connect(&m_wordCountTimer, &QTimer::timeout, this, &Backend::refreshWordCount);
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(750);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &Backend::writeRecovery);
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                if (path != m_fileUrl.toLocalFile())
                    return;

                const bool deleted = !QFileInfo::exists(path);
                if (!deleted && m_hasKnownFileContents) {
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)
                            && file.readAll() == m_lastKnownFileContents) {
                        // Atomic saves can replace the watched inode. Re-arm the
                        // watcher, but do not report our own save as an outside edit.
                        watchCurrentFile();
                        return;
                    }
                }

                emit externalChangeDetected(deleted, m_modified);
            });

    loadPywalTheme();
    watchPywalTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadPywalTheme();
        watchPywalTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadPywalTheme();
        watchPywalTheme();
    });
}

Backend::~Backend() = default;

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

QString Backend::fileName() const {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty())
        return QStringLiteral("Untitled.md");

    if (m_fileUrl.isLocalFile()) {
        const QFileInfo info(m_fileUrl.toLocalFile());
        if (!info.fileName().isEmpty())
            return info.fileName();
    }

    const QString name = m_fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

void Backend::setDarkMode(bool darkMode) {
    // pywal's palette decides light versus dark whenever it is available. The
    // desktop portal only gets a say when pywal has never run on this machine.
    if (m_pywalLoaded || m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadPywalTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::attachDocument(QObject *textDocument) {
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (!quickDocument || !quickDocument->textDocument()) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    if (m_highlighter)
        delete m_highlighter.data();

    m_document = quickDocument->textDocument();
    m_lastDocumentText = m_document->toPlainText();
    m_highlighter = new MarkdownHighlighter(m_document);
    m_highlighter->setDarkMode(m_darkMode);
    m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent,
                             m_themeMuted, m_themeSurface);

    connect(m_document, &QTextDocument::contentsChange, this,
            [this](int position, int, int charsAdded) {
                if (m_formattingTypography || m_loading)
                    return;
                m_lastChangePos = position;
                m_lastChangeAdded = charsAdded;
            });

    applyDocumentTypography();
    if (m_recoverOrphans)
        restoreRecovery();
}

void Backend::openDialog() {
    emit openDialogRequested();
}

void Backend::open(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return;
    }

    const QFileInfo info(url.toLocalFile());
    const QString targetName = info.fileName();

    // Directories, devices, FIFOs and sockets. Reading /dev/zero would never
    // return, and a directory just fails confusingly further down.
    if (!info.isFile()) {
        setStatus(info.isDir()
            ? QStringLiteral("%1 is a folder.").arg(targetName)
            : QStringLiteral("%1 is not a regular file.").arg(targetName));
        return;
    }

    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not open %1.").arg(targetName));
        return;
    }

    // Read one byte past the cap rather than trusting the reported size: files
    // under /proc report zero and still have contents, and a log can grow
    // between the stat and the read.
    const QByteArray contents = file.read(maximumOpenSize + 1);
    if (contents.size() > maximumOpenSize) {
        setStatus(QStringLiteral("%1 is too large to open.").arg(targetName));
        return;
    }

    // A NUL byte means this is not text. Loading it would fill the editor with
    // replacement characters and quietly corrupt the file if it were saved back.
    if (contents.contains('\0')) {
        setStatus(QStringLiteral("%1 is not a text file.").arg(targetName));
        return;
    }

    loadDocumentText(QString::fromUtf8(contents));
    clearRecovery();
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(url);
    watchCurrentFile();
    setModified(false);
    setStatus(QStringLiteral("Opened %1").arg(fileName()));
}

void Backend::save() {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty()) {
        saveAsDialog();
        return;
    }

    saveTo(m_fileUrl);
}

void Backend::saveForClose() {
    if (!m_modified) {
        emit closeAfterSave();
        return;
    }

    m_closeAfterSave = true;
    save();
}

void Backend::saveAsDialog() {
    emit saveDialogRequested(suggestedSaveUrl());
}

void Backend::saveAs(const QUrl &url) {
    saveTo(url);
}

void Backend::fileDialogCanceled() {
    m_closeAfterSave = false;
}

void Backend::discardRecovery() {
    clearRecovery();
}

void Backend::discardChanges() {
    clearRecovery();
    setModified(false);
    setStatus(QStringLiteral("Discarded unsaved changes"));
}

void Backend::reloadFromDisk() {
    if (m_fileUrl.isLocalFile())
        open(m_fileUrl);
}

void Backend::keepExternalVersion() {
    QFile file(m_fileUrl.toLocalFile());
    if (file.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = file.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setModified(true);
    scheduleRecovery();
    watchCurrentFile();
    setStatus(QStringLiteral("Kept your version"));
}

void Backend::printDocument() {
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to print."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("Print %1").arg(fileName()));
    dialog.winId();
    if (dialog.windowHandle() && m_parentWindow)
        dialog.windowHandle()->setTransientParent(m_parentWindow);

    if (dialog.exec() == QDialog::Accepted) {
        QTextDocument rendered;
        rendered.setDefaultFont(m_document->defaultFont());
        rendered.setMarkdown(currentDocumentText());
        rendered.print(&printer);
    }
}


QString Backend::clipboardUrl() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData)
        return {};

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString normalized = normalizedLinkUrl(url.toString());
            if (!normalized.isEmpty())
                return normalized;
        }
    }

    if (!mimeData->hasText())
        return {};

    return normalizedLinkUrl(mimeData->text());
}

QString Backend::clipboardText() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    return mimeData && mimeData->hasText() ? mimeData->text() : QString();
}

bool Backend::editorTextChanged() {
    if (m_loading || m_formattingTypography)
        return false;

    const QString text = currentDocumentText();
    if (text == m_lastDocumentText)
        return false;
    m_lastDocumentText = text;

    if (m_document) {
        const int blockCount = m_document->blockCount();
        if (blockCount > m_formattedBlockCount)
            reapplyTypographyToChange();
        m_formattedBlockCount = blockCount;
    }

    scheduleWordCount();
    setModified(true);
    setStatus(QStringLiteral("Unsaved"));
    scheduleRecovery();
    return true;
}

QVariantList Backend::hiddenRangesAt(int position) const {
    QVariantList ranges;
    if (!m_document)
        return ranges;

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return ranges;

    const int lineStart = block.position();
    QList<QPair<int, int>> spans;
    const QList<MarkdownHighlighter::InlineMarkup> markup =
        MarkdownHighlighter::inlineMarkup(block.text());
    for (const MarkdownHighlighter::InlineMarkup &item : markup) {
        for (const MarkdownHighlighter::Span &marker : item.markers) {
            spans.append({lineStart + marker.start,
                          lineStart + marker.start + marker.length});
        }
    }
    std::sort(spans.begin(), spans.end());

    for (const auto &span : spans) {
        ranges.append(QVariantMap{{QStringLiteral("start"), span.first},
                                  {QStringLiteral("end"), span.second}});
    }
    return ranges;
}

void Backend::setSearchHighlight(const QString &query, int currentMatchStart) {
    if (m_highlighter)
        m_highlighter->setSearch(query, currentMatchStart);
}

void Backend::openExternalUrl(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("mailto"))
        QDesktopServices::openUrl(url);
}

QVariantMap Backend::windowGeometry() const {
    QSettings settings;
    return {{QStringLiteral("x"), settings.value(QStringLiteral("window/x"), -1)},
            {QStringLiteral("y"), settings.value(QStringLiteral("window/y"), -1)},
            {QStringLiteral("width"), settings.value(QStringLiteral("window/width"), 1280)},
            {QStringLiteral("height"), settings.value(QStringLiteral("window/height"), 820)},
            {QStringLiteral("maximized"), settings.value(QStringLiteral("window/maximized"), false)}};
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    if (!maximized) {
        settings.setValue(QStringLiteral("window/x"), x);
        settings.setValue(QStringLiteral("window/y"), y);
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void Backend::loadDocumentText(const QString &text) {
    if (!m_document) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    m_loading = true;
    m_document->setPlainText(text);
    m_lastDocumentText = text;
    m_loading = false;

    applyDocumentTypography();
    m_wordCountTimer.stop();
    setWordCount(countWords(text));
}

void Backend::setFileUrl(const QUrl &url) {
    if (m_fileUrl == url)
        return;

    m_fileUrl = url;
    emit fileUrlChanged();
    watchCurrentFile();
}

void Backend::setModified(bool modified) {
    if (m_modified == modified)
        return;

    m_modified = modified;
    emit modifiedChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void Backend::saveTo(const QUrl &url) {
    if (!url.isLocalFile()) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Only local files can be saved."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not save %1.").arg(targetName));
        return;
    }

    const QByteArray contents = currentDocumentText().toUtf8();
    file.write(contents);

    // QSaveFile commits by replacing the target. Stop watching the old inode
    // before that replacement so our own write is not classified as external.
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);

    // commit() flushes, fsyncs, and atomically renames the temp file into place,
    // returning false (and leaving the original untouched) on any write error.
    if (!file.commit()) {
        watchCurrentFile();
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not write %1.").arg(targetName));
        return;
    }

    const bool shouldClose = m_closeAfterSave;
    m_closeAfterSave = false;
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(url);
    watchCurrentFile();
    QSettings().setValue(lastSaveDirectorySetting,
                         QFileInfo(url.toLocalFile()).absolutePath());
    setModified(false);
    setStatus(QStringLiteral("Saved %1").arg(fileName()));
    clearRecovery();
    emit saveSucceeded();

    if (shouldClose)
        emit closeAfterSave();
}

void Backend::scheduleRecovery() {
    m_recoveryTimer.start();
}

QString Backend::recoveryPath() const {
    return m_recoveryPath;
}

void Backend::writeRecovery() {
    if (!m_modified)
        return;
    const QString path = recoveryPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject recovery{{QStringLiteral("fileUrl"), m_fileUrl.toString()},
                               {QStringLiteral("text"), currentDocumentText()}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    file.commit();
}

void Backend::restoreRecovery() {
    QFile file(recoveryPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || !json.object().contains(QStringLiteral("text")))
        return;
    const QJsonObject recovery = json.object();
    loadDocumentText(recovery.value(QStringLiteral("text")).toString());
    const QUrl recoveredUrl(recovery.value(QStringLiteral("fileUrl")).toString());
    QFile diskFile(recoveredUrl.toLocalFile());
    if (recoveredUrl.isLocalFile() && diskFile.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = diskFile.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setFileUrl(recoveredUrl);
    setModified(true);
    setStatus(QStringLiteral("Recovered unsaved changes"));
}

void Backend::clearRecovery() {
    m_recoveryTimer.stop();
    QFile::remove(recoveryPath());
}

void Backend::watchCurrentFile() {
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile()))
        m_fileWatcher.addPath(m_fileUrl.toLocalFile());
}

namespace {
QString walDirectory() {
    return QDir::homePath() + QStringLiteral("/.cache/wal");
}

// Blend `from` toward `to` in sRGB; ratio 0 keeps `from`, 1 returns `to`.
QColor blend(const QColor &from, const QColor &to, qreal ratio) {
    if (!from.isValid())
        return to;
    if (!to.isValid())
        return from;

    const qreal keep = 1.0 - ratio;
    return QColor::fromRgbF(from.redF() * keep + to.redF() * ratio,
                            from.greenF() * keep + to.greenF() * ratio,
                            from.blueF() * keep + to.blueF() * ratio);
}

qreal luminance(const QColor &color) {
    return 0.299 * color.redF() + 0.587 * color.greenF() + 0.114 * color.blueF();
}

// Read pywal's current palette. colors.json is the canonical output; the plain
// `colors` file is the fallback for setups whose template set omits the JSON.
bool readPywalPalette(QColor *background, QColor *foreground, QList<QColor> *palette) {
    QFile json(walDirectory() + QStringLiteral("/colors.json"));
    if (json.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(json.readAll()).object();
        const QJsonObject special = root.value(QStringLiteral("special")).toObject();
        const QJsonObject colors = root.value(QStringLiteral("colors")).toObject();

        const QColor page(special.value(QStringLiteral("background")).toString());
        const QColor ink(special.value(QStringLiteral("foreground")).toString());
        if (page.isValid() || ink.isValid()) {
            if (page.isValid())
                *background = page;
            if (ink.isValid())
                *foreground = ink;
            for (int index = 0; index < 16; ++index) {
                palette->append(QColor(
                    colors.value(QStringLiteral("color%1").arg(index)).toString()));
            }
            return true;
        }
    }

    QFile plain(walDirectory() + QStringLiteral("/colors"));
    if (!plain.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&plain);
    while (!in.atEnd() && palette->size() < 16)
        palette->append(QColor(in.readLine().trimmed()));

    if (palette->isEmpty() || !palette->constFirst().isValid()) {
        palette->clear();
        return false;
    }

    // The plain file carries no special section, so fall back to the convention
    // every pywal template uses: color0 is the page and color7 the ink.
    *background = palette->constFirst();
    if (palette->size() > 7 && palette->at(7).isValid())
        *foreground = palette->at(7);
    return true;
}

// pywal has no notion of an accent, so take the first usable entry in the order
// its own templates favour, skipping anything that would vanish into the page.
QColor pickAccent(const QList<QColor> &palette, const QColor &background,
                  const QColor &foreground) {
    static const int preferred[] = {4, 5, 6, 2, 3, 1, 12, 13, 14};
    const qreal page = luminance(background);
    for (const int index : preferred) {
        if (index >= palette.size())
            continue;

        const QColor candidate = palette.at(index);
        if (candidate.isValid() && qAbs(luminance(candidate) - page) >= 0.12)
            return candidate;
    }

    return foreground;
}
}

void Backend::loadPywalTheme() {
    // Stand-ins for a machine where pywal has never run; the desktop portal
    // still picks which of the two sets applies.
    QColor background(m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff"));
    QColor foreground(m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324"));
    QList<QColor> palette;

    m_pywalLoaded = readPywalPalette(&background, &foreground, &palette);

    const QColor accent = m_pywalLoaded
        ? pickAccent(palette, background, foreground)
        : QColor(m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2"));

    // Selected text keeps the foreground colour, so pull the accent back toward
    // the page to leave the words on top of the fill legible.
    const QColor selection = blend(accent, background, 0.45);

    // color8 is pywal's dim grey. Where it is missing, or too close to the page
    // to read, meet the foreground partway instead.
    QColor muted = palette.size() > 8 ? palette.at(8) : QColor();
    if (!muted.isValid() || qAbs(luminance(muted) - luminance(background)) < 0.1)
        muted = blend(background, foreground, 0.45);

    // Code spans and the search bar sit just off the page in either direction.
    const QColor surface = blend(background, foreground, 0.08);

    m_themeBackground = background.name();
    m_themeForeground = foreground.name();
    m_themeAccent = accent.name();
    m_themeSelection = selection.name();
    m_themeMuted = muted.name();
    m_themeSurface = surface.name();

    const bool themeIsDark = luminance(background) < 0.5;
    if (themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    if (m_highlighter) {
        m_highlighter->setDarkMode(m_darkMode);
        m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent,
                                 m_themeMuted, m_themeSurface);
    }

    emit themeColorsChanged();
}

void Backend::watchPywalTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString directory = walDirectory();

    // Watch the directory as well as the files: `wal` replaces its output
    // rather than editing it in place, so a file watch goes stale after the
    // first theme change and a missing file has to be picked up when it lands.
    if (QDir(directory).exists())
        m_themeWatcher.addPath(directory);

    for (const QString &name : {QStringLiteral("colors.json"), QStringLiteral("colors")}) {
        const QString path = directory + QLatin1Char('/') + name;
        if (QFile::exists(path))
            m_themeWatcher.addPath(path);
    }
}

// The in-app file browser's sidebar. Only offer places that actually exist, so
// a machine without an ~/XDG user-dirs setup does not get dead entries.
QVariantList Backend::standardPlaces() const {
    const struct { QStandardPaths::StandardLocation location; const char *name; } places[] = {
        {QStandardPaths::HomeLocation, "Home"},
        {QStandardPaths::DesktopLocation, "Desktop"},
        {QStandardPaths::DocumentsLocation, "Documents"},
        {QStandardPaths::DownloadLocation, "Downloads"},
        {QStandardPaths::PicturesLocation, "Pictures"},
        {QStandardPaths::MusicLocation, "Music"},
        {QStandardPaths::MoviesLocation, "Videos"},
    };

    QVariantList result;
    QStringList seen;
    for (const auto &place : places) {
        const QString path = QStandardPaths::writableLocation(place.location);
        if (path.isEmpty() || seen.contains(path) || !QFileInfo(path).isDir())
            continue;

        seen.append(path);
        result.append(QVariantMap{
            {QStringLiteral("name"), QString::fromLatin1(place.name)},
            {QStringLiteral("url"), QUrl::fromLocalFile(path)},
        });
    }

    result.append(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("Filesystem")},
        {QStringLiteral("url"), QUrl::fromLocalFile(QStringLiteral("/"))},
    });
    return result;
}

// The folder one level up, or an empty URL at the filesystem root. The browser
// used to derive this from the breadcrumbs, which made $HOME a dead end: it
// collapses to a single "~" crumb with no parent to walk back to.
QUrl Backend::parentFolder(const QUrl &folder) const {
    if (!folder.isLocalFile())
        return {};

    QDir directory(folder.toLocalFile());
    if (directory.isRoot() || !directory.cdUp())
        return {};

    return QUrl::fromLocalFile(directory.absolutePath());
}

// Path segments for the browser's breadcrumb bar, shortened to "~" once the
// path enters the home directory.
QVariantList Backend::folderCrumbs(const QUrl &folder) const {
    if (!folder.isLocalFile())
        return {};

    const QString home = QDir::cleanPath(QDir::homePath());
    QString path = QDir::cleanPath(folder.toLocalFile());

    QVariantList crumbs;
    const auto prepend = [&crumbs](const QString &name, const QString &target) {
        crumbs.prepend(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("url"), QUrl::fromLocalFile(target)},
        });
    };

    while (!path.isEmpty()) {
        if (path == home) {
            prepend(QStringLiteral("~"), path);
            return crumbs;
        }
        if (path == QStringLiteral("/")) {
            prepend(QStringLiteral("/"), path);
            return crumbs;
        }

        const int slash = path.lastIndexOf(QLatin1Char('/'));
        if (slash < 0)
            return crumbs;

        prepend(path.mid(slash + 1), path);
        path = slash == 0 ? QStringLiteral("/") : path.left(slash);
    }
    return crumbs;
}

// Both dialogs always start in the home folder. Following the open document or
// the last-used directory meant a single excursion into somewhere like /etc
// became the starting point for every later open and save.
QUrl Backend::startFolder() const {
    return QUrl::fromLocalFile(QDir::homePath());
}

// Save As is the one exception: it starts beside the document being saved, so
// "save a copy next to the original" stays a single step. A document that has
// never been written anywhere still starts at home.
QUrl Backend::saveStartFolder() const {
    if (m_fileUrl.isLocalFile()) {
        const QString directory = QFileInfo(m_fileUrl.toLocalFile()).absolutePath();
        if (!directory.isEmpty() && QDir(directory).exists())
            return QUrl::fromLocalFile(directory);
    }

    return startFolder();
}

bool Backend::fileExists(const QUrl &url) const {
    return url.isLocalFile() && QFileInfo::exists(url.toLocalFile());
}

QUrl Backend::folderChild(const QUrl &folder, const QString &name) const {
    if (!folder.isLocalFile() || name.isEmpty())
        return {};

    return QUrl::fromLocalFile(QDir(folder.toLocalFile()).filePath(name));
}

QString Backend::fileNameOf(const QUrl &url) const {
    if (!url.isLocalFile())
        return QStringLiteral("Untitled.md");

    const QString name = QFileInfo(url.toLocalFile()).fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

QUrl Backend::suggestedSaveUrl() const {
    if (m_fileUrl.isLocalFile())
        return m_fileUrl;

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);
    return QUrl::fromLocalFile(
        directory.filePath(suggestedFileName(currentDocumentText())));
}

QString Backend::currentDocumentText() const {
    return m_document ? m_document->toPlainText() : QString();
}

int Backend::countWords(const QString &text) {
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['-][\\p{L}\\p{N}]+)*"));
    int count = 0;
    QRegularExpressionMatchIterator it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

QString Backend::suggestedFileName(const QString &text) {
    QString name = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    name.replace(QRegularExpression(QStringLiteral("[/\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
    name = name.left(120).trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("Untitled");
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

void Backend::setWordCount(int words) {
    if (m_wordCount == words)
        return;

    m_wordCount = words;
    emit wordCountChanged();
}

void Backend::refreshWordCount() {
    setWordCount(countWords(currentDocumentText()));
}

void Backend::scheduleWordCount() {
    m_wordCountTimer.start();
}

void Backend::applyDocumentTypography() {
    if (!m_document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    // A full pass is only used for freshly loaded/attached documents, so it is
    // safe to drop undo history here (re-enabling clears the stack anyway).
    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(blockFormat);
    m_formattingTypography = false;

    m_document->setUndoRedoEnabled(undoEnabled);

    m_formattedBlockCount = m_document->blockCount();
}

void Backend::reapplyTypographyToChange() {
    if (!m_document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    // Format only the block(s) touched by the last edit instead of the whole
    // document, and fold the change into the preceding edit command so a single
    // undo reverts both the text and its formatting.
    const int maxPos = m_document->characterCount() - 1;
    const int start = qBound(0, m_lastChangePos, maxPos);
    const int end = qBound(start, m_lastChangePos + m_lastChangeAdded, maxPos);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.joinPreviousEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.mergeBlockFormat(blockFormat);
    cursor.endEditBlock();
    m_formattingTypography = false;
}
