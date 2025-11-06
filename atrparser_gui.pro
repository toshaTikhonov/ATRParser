QT += core gui widgets

TARGET = atrparser_gui
TEMPLATE = app

CONFIG += c++17

# Source files
SOURCES += \
    main.cpp \
    atrparser.cpp \
    cardreader.cpp

HEADERS += \
    atrparser.h \
    cardreader.h

# PC/SC Lite library
unix {
    LIBS += -lpcsclite
    INCLUDEPATH += /usr/include/PCSC
}

macx {
    LIBS += -framework PCSC
    INCLUDEPATH += /System/Library/Frameworks/PCSC.framework/Headers
}

win32 {
    LIBS += -lwinscard
    INCLUDEPATH += "C:/Program Files/PCSC/include"
}

# Install
target.path = /usr/local/bin
INSTALLS += target
