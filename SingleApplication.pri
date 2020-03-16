QT += core network
CONFIG += c++11

INCLUDEPATH += $$PWD

win32 {
    msvc:LIBS += Advapi32.lib
    gcc:LIBS += -ladvapi32
}

HEADERS += \
    $$PWD/SingleApplication.h \
    $$PWD/SingleApplication_p.h
    
SOURCES += \
    $$PWD/SingleApplication.cpp \
    $$PWD/SingleApplication_p.cpp

DISTFILES += \
    $$PWD/README.md \
    $$PWD/CHANGELOG.md \
    $$PWD/LICENSE \
    $$PWD/Windows.md
