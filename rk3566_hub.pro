QT       += core gui widgets
CONFIG   += c++17
TARGET   = rk3566_hub
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/ui/main_window.cpp \
    src/vision/hub_v4l2_capture.c \
    src/vision/hub_presence.c \
    src/vision/camera_worker.cpp \
    src/vision/presence_guard.cpp \
    src/vision/detector.cpp \
    src/vision/rknn_engine.c \
    src/net/hub_mqtt.c \
    src/net/hub_protocol.c \
    src/net/mqtt_client.cpp \
    src/net/protocol.cpp \
    src/storage/cJSON.c \
    src/storage/hub_db.c \
    src/storage/database.cpp

HEADERS += \
    src/ui/main_window.h \
    src/vision/hub_v4l2_capture.h \
    src/vision/hub_presence.h \
    src/vision/camera_worker.h \
    src/vision/presence_guard.h \
    src/vision/detector.h \
    src/vision/rknn_engine.h \
    src/net/hub_mqtt.h \
    src/net/hub_protocol.h \
    src/net/mqtt_client.h \
    src/net/protocol.h \
    src/storage/cJSON.h \
    src/storage/hub_db.h \
    src/storage/database.h

INCLUDEPATH += src third_party/rknn/include

CONFIG += link_pkgconfig
PKGCONFIG += libmosquitto sqlite3

# 板子上若已安装 librknnrt：
LIBS += -lrknnrt -lm
DEFINES += HUB_HAVE_RKNN=1
