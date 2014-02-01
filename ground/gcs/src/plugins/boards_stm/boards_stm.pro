TEMPLATE = lib
TARGET = Stm
include(../../taulabsgcsplugin.pri)
include(../../plugins/uavobjects/uavobjects.pri)
include(../../plugins/coreplugin/coreplugin.pri)

OTHER_FILES += Stm.pluginspec

HEADERS += \
    stmplugin.h \
    flyingf3.h \
    flyingf4.h \
    core4.h \
    discoveryf4.h

SOURCES += \
    stmplugin.cpp \
    flyingf3.cpp \
    flyingf4.cpp \
    coref4.cpp \
    discoveryf4.cpp

RESOURCES += \
    stm.qrc

