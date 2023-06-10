#-------------------------------------------------
#
# Project created by QtCreator 2011-12-14T20:57:35
#
#-------------------------------------------------

QT       += core gui widgets

TARGET = SIMMProgrammer
TEMPLATE = app
QMAKE_TARGET_BUNDLE_PREFIX = com.downtowndougbrown

SOURCES += main.cpp\
        mainwindow.cpp \
    programmer.cpp \
    aboutbox.cpp

HEADERS  += mainwindow.h \
    programmer.h \
    aboutbox.h

FORMS    += mainwindow.ui \
    aboutbox.ui

linux*:CONFIG += qesp_linux_udev
include(3rdparty/qextserialport/src/qextserialport.pri)

QMAKE_CXXFLAGS_RELEASE += -DQT_NO_DEBUG_OUTPUT

macx:CONFIG += x86
macx:CONFIG += x86_64

OTHER_FILES += \
    SIMMProgrammer.rc \
    Info.plist

RC_FILE = SIMMProgrammer.rc

ICON = SIMMProgrammer.icns

VERSION = 1.1.2
DEFINES += VERSION_STRING=\\\"$$VERSION\\\"

macx:QMAKE_INFO_PLIST = Info.plist
