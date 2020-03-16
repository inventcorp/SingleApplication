QT += core network
CONFIG += c++11

HEADERS += $$PWD/SingleApplication \
    $$PWD/SingleApplication.h \
    $$PWD/SingleApplication_p.h
    
SOURCES += $$PWD/SingleApplication.cpp \
    $$PWD/SingleApplication_p.cpp

INCLUDEPATH += $$PWD

win32 {
    msvc:LIBS += Advapi32.lib
    gcc:LIBS += -ladvapi32
}

DISTFILES += \
    $$PWD/README.md \
    $$PWD/CHANGELOG.md \
    $$PWD/Windows.md
