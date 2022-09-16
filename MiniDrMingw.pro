#-------------------------------------------------
#
# Project created by QtCreator 2022-09-09T14:06:00
#
#-------------------------------------------------
QT += core

TARGET = MiniDrMingw
TEMPLATE = lib
DESTDIR = bin

CONFIG += skip_target_version_ext

DEFINES += __SHARE_EXPORT

win32{
QMAKE_LFLAGS += -Wl,--add-stdcall-alias
}

LIBS += -lpsapi -lversion -ldbghelp

HEADERS += \
    dllexport.h \
    log.h \
    formatoutput.h \
    util.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}

SOURCES += \
    main.cpp
