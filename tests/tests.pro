QT += core gui quick testlib
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_wwrite

INCLUDEPATH += ../src
SOURCES += \
    tst_wwrite.cpp \
    ../src/backend.cpp \
    ../src/markdownhighlighter.cpp
HEADERS += \
    ../src/backend.h \
    ../src/markdownhighlighter.h

QT += widgets printsupport quickcontrols2 dbus
