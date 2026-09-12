#include <QFont>
#include <QFontDatabase>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>
#include <QWindow>
#include <QFile>

#include "backend.h"
#include "session.h"
#include "systemtheme.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // QSettings with no organization name writes to a literal
    // "Unknown Organization" directory, so give it one.
    app.setOrganizationName(QStringLiteral("wwrite"));
    app.setApplicationName(QStringLiteral("wwrite"));
    app.setApplicationDisplayName(QStringLiteral("wwrite"));
    app.setDesktopFileName(QStringLiteral("wwrite"));
    // Theme lookup only works once a platform theme plugin has added the XDG
    // icon paths, which is not guaranteed. Fall back to the copy compiled into
    // the binary so the window always carries an icon.
    QIcon icon = QIcon::fromTheme(QStringLiteral("wwrite"));
    if (icon.isNull())
        icon = QIcon(QStringLiteral(":/wwrite.svg"));
    app.setWindowIcon(icon);

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Italic.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-BoldItalic.ttf"));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    SystemTheme systemTheme(&app);
    Session session(&systemTheme, &app);

    // Carry the desktop's text scale into the default font, so the chrome that
    // inherits it (dialog titles, buttons) grows along with the writing area.
    const QFont interfaceFont(QStringLiteral("iA Writer Mono S"));
    const qreal basePointSize = interfaceFont.pointSizeF() > 0
        ? interfaceFont.pointSizeF()
        : app.font().pointSizeF();
    const auto applyInterfaceFont = [&app, interfaceFont, basePointSize](qreal textScale) {
        QFont scaled = interfaceFont;
        scaled.setPointSizeF(basePointSize * textScale);
        app.setFont(scaled);
    };
    applyInterfaceFont(systemTheme.textScale());

    // Session keeps each document's own text scale in step; this only has to
    // carry the change into the application font the chrome inherits.
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &app,
                     [applyInterfaceFont](qreal textScale) {
        applyInterfaceFont(textScale);
    });

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("session"), &session);

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the wwrite interface; resource available:"
                    << QFile::exists(QStringLiteral(":/Main.qml"));
        return -1;
    }

    auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());

    // A file per argument, each in its own document, so `wwrite a.md b.md`
    // opens both rather than only the first.
    const QStringList args = app.arguments();
    for (int index = 1; index < args.size(); ++index) {
        if (index > 1)
            session.newDocument();
        if (Backend *document = session.current()) {
            if (!document->modified())
                document->open(QUrl::fromLocalFile(args.at(index)));
        }
    }
    session.setCurrentIndex(0);

    for (int index = 0; index < session.count(); ++index) {
        if (Backend *document = session.at(index))
            document->setParentWindow(window);
    }

    return app.exec();
}
