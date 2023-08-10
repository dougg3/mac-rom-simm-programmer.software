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
    3rdparty/fc8-compression.c \
    chipid.cpp \
    createblankdiskdialog.cpp \
    droppablegroupbox.cpp \
    fc8compressor.cpp \
    labelwithlinks.cpp \
    mainwindow.cpp \
    programmer.cpp \
    aboutbox.cpp \
    textbrowserwithlinks.cpp

HEADERS  += mainwindow.h \
    3rdparty/fc8-compression/fc8.h \
    chipid.h \
    createblankdiskdialog.h \
    droppablegroupbox.h \
    fc8compressor.h \
    labelwithlinks.h \
    programmer.h \
    aboutbox.h \
    textbrowserwithlinks.h

FORMS    += mainwindow.ui \
    aboutbox.ui \
    createblankdiskdialog.ui

linux*:CONFIG += qesp_linux_udev
include(3rdparty/qextserialport/src/qextserialport.pri)

QMAKE_CXXFLAGS_RELEASE += -DQT_NO_DEBUG_OUTPUT

macx:CONFIG += x86
macx:CONFIG += x86_64

OTHER_FILES += \
    SIMMProgrammer.rc \
    Info.plist

macx:ICON = SIMMProgrammer.icns
lessThan(QT_MAJOR_VERSION, 5) {
    # Older Qt required manual resource file for adding icons
	win32:RC_FILE = SIMMProgrammer.rc
} else {
    # Newer Qt does it automatically, and adds version info too
	win32:RC_ICONS = SIMMProgrammer.ico
}

VERSION = 2.0.0
DEFINES += VERSION_STRING=\\\"$$VERSION\\\"

macx:QMAKE_INFO_PLIST = Info.plist
win32:QMAKE_TARGET_COMPANY = "Doug Brown"
win32:QMAKE_TARGET_DESCRIPTION = "Mac ROM SIMM Programmer"
win32:QMAKE_TARGET_COPYRIGHT = "Copyright (C) Doug Brown"
win32:QMAKE_TARGET_PRODUCT = "Mac ROM SIMM Programmer"

DISTFILES += \
    chipid.txt

RESOURCES += \
    chipid.qrc
