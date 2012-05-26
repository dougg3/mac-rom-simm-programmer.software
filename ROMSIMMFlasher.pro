#-------------------------------------------------
#
# Project created by QtCreator 2011-12-14T20:57:35
#
#-------------------------------------------------

QT       += core gui

TARGET = SIMMProgrammer
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp \
    programmer.cpp \
    aboutbox.cpp

HEADERS  += mainwindow.h \
    programmer.h \
    aboutbox.h \
    version.h

FORMS    += mainwindow.ui \
    aboutbox.ui

include(../doug-qextserialport-linuxnotifications/src/qextserialport.pri)

QMAKE_CXXFLAGS_RELEASE += -DQT_NO_DEBUG_OUTPUT
