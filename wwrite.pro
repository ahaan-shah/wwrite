QT += core gui widgets printsupport qml quick quickcontrols2 dbus svg

CONFIG += c++17 release
TARGET = wwrite
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/markdownhighlighter.h \
    src/session.h \
    src/systemtheme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/markdownhighlighter.cpp \
    src/session.cpp \
    src/systemtheme.cpp

RESOURCES += src/resources.qrc
