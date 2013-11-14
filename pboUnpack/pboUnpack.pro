#====================================================================
#          PROJECT CONFIGURATION
#====================================================================

message("Processing $${_FILE_}")

CONFIG(debug, debug|release):BUILD_TARGET = debug
CONFIG(release, debug|release):BUILD_TARGET = release
message("Building [$${BUILD_TARGET}] Makefile for [$${TARGET}] on [$${QMAKE_HOST.os}] [$${QMAKE_HOST.arch}]")

# Project configuration and compiler options
#TARGET = target # If not defined: same as the file name
TEMPLATE = app
CONFIG -= qt
#QT += core gui
#greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

# Build locations
DESTDIR = bin
MOC_DIR = tmp
OBJECTS_DIR = $$MOC_DIR
RCC_DIR = $$MOC_DIR
UI_DIR = $$MOC_DIR
unix:QMAKE_DISTCLEAN += -r $$MOC_DIR

# Additional configuration
#DEFINES += MY_CODE=1
#DEFINES += MY_TEXT=\\\"This is my text\\\"

*g++* {
    # Enable support for C++11 language revision
    QMAKE_CXXFLAGS += -std=c++0x

    # Disable some warnings, make all the others into errors
    QMAKE_CXXFLAGS += -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable
    #QMAKE_CXXFLAGS += -Werror

    # Disable standard-C assertions
    QMAKE_CFLAGS_RELEASE    +=  -DNDEBUG
    QMAKE_CXXFLAGS_RELEASE  +=  -DNDEBUG
}

win32-msvc* {
    # Disable deprecation of *printf functions
    DEFINES += _CRT_SECURE_NO_WARNINGS

    # Disable standard-C assertions
    QMAKE_CFLAGS_RELEASE    +=  /DNDEBUG
    QMAKE_CXXFLAGS_RELEASE  +=  /DNDEBUG
}



#====================================================================
#          PROJECT FILES
#====================================================================

# Dependencies: GLUT (freeglut3-dev)

INCLUDEPATH += \
    src

DEPENDPATH += \
    src

LIBS += -lglut -lGLU -lGL -lm

HEADERS += \
    src/glInfo.h \
    src/Timer.h

SOURCES += src/main.cpp \
    src/glInfo.cpp \
    src/Timer.cpp

#FORMS += MyForm.ui
#RESOURCES += resources.qrc
#OTHER_FILES += readme.txt
