#-------------------------------------------------
#
# Project created by QtCreator 2011-12-14T20:57:35
#
#-------------------------------------------------

QT       += core gui

TARGET = ROMSIMMFlasher
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp \
    usbprogrammerfinder_win.cpp \
    programmer.cpp

HEADERS  += mainwindow.h \
    usbprogrammerfinder.h \
    programmer.h

FORMS    += mainwindow.ui

include(../qextserialport/src/qextserialport.pri)

QMAKE_CXXFLAGS_RELEASE += -DQT_NO_DEBUG_OUTPUT
