QT += core
QT -= gui

CONFIG += c++11

TARGET = DrMingwDemo
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

DESTDIR = $$PWD/bin

LIBS += -L$$PWD/lib -lMiniDrMingw

INCLUDEPATH += $$PWD/lib/include

# gcc ����ѡ����� .map �ļ�
QMAKE_LFLAGS += -Wl,-Map=$$PWD'/bin/'$$TARGET'.map'

SOURCES += main.cpp

QMAKE_CXXFLAGS_RELEASE = $$QMAKE_CFLAGS_RELEASE_WITH_DEBUGINFO
QMAKE_LFLAGS_RELEASE = $$QMAKE_LFLAGS_RELEASE_WITH_DEBUGINFO

HEADERS +=
