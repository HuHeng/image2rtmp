TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

DEFINES = __STDC_CONSTANT_MACROS

INCLUDEPATH=/home/huheng/source/ffmpeg

LIBS=-lavcodec -lavfilter -lavutil -lswresample \
-lavformat -lswscale  -lavdevice -lpostproc -pthread

SOURCES += \
    main.cpp \
    image2rtmp.cpp \
    simplefilter.cpp

HEADERS += \
    image2rtmp.h \
    simplefilter.h
