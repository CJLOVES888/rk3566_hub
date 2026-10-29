QT       += core gui widgets sql
CONFIG   += c++17
TARGET   = rk3566_hub
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/ui/main_window.cpp \
    src/vision/camera_worker.cpp \
    src/vision/v4l2_mplane_capture.cpp \
    src/vision/presence_guard.cpp \
    src/vision/detector.cpp \
    src/vision/rknn_engine.c \
    src/net/mqtt_client.cpp \
    src/storage/database.cpp

HEADERS += \
    src/ui/main_window.h \
    src/vision/camera_worker.h \
    src/vision/v4l2_mplane_capture.h \
    src/vision/presence_guard.h \
    src/vision/detector.h \
    src/vision/rknn_engine.h \
    src/net/mqtt_client.h \
    src/net/protocol.h \
    src/storage/database.h

INCLUDEPATH += src third_party/rknn/include

CONFIG += link_pkgconfig
PKGCONFIG += opencv4 libmosquitto

# 板子上若已安装 librknnrt：
LIBS += -lrknnrt -lm
DEFINES += HUB_HAVE_RKNN=1
