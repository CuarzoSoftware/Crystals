QT += core gui widgets
CONFIG += c++17
TARGET = Crystals
target.path = /usr/bin
INSTALLS += target

LIBS += \
    -lsystemd \
    -lLouvre 

INCLUDEPATH += \
    /usr/include/systemd \
    /usr/include/Louvre \
    /usr/include/drm \

HEADERS += \
    code/Compositor.h \
    code/Output.h \
    code/Pointer.h \
    code/Popup.h \
    code/Surface.h \
    code/SurfaceView.h \
    code/ToplevelRole.h \
    code/View.h

SOURCES += \
    code/Compositor.cpp \
    code/Output.cpp \
    code/Pointer.cpp \
    code/Popup.cpp \
    code/Surface.cpp \
    code/SurfaceView.cpp \
    code/ToplevelRole.cpp \
    code/View.cpp \
    code/main.cpp

