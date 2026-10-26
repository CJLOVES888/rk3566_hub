/*
 * ============================================================================
 * main.cpp —— RK3566 智能中控终端入口
 * ----------------------------------------------------------------------------
 * 模块：UI / 摄像头 / MQTT / SQLite / NPU Detector
 *
 * 环境变量：
 *   HUB_MQTT_UID      巴法云私钥
 *   HUB_RKNN_MODEL    yolov8n.rknn 绝对路径（可选）
 *   QT_QPA_PLATFORM=linuxfb ...
 * ============================================================================
 */

#include <QApplication>
#include <QScreen>
#include <QDebug>

#include "ui/main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    ui::MainWindow window;
    if (QScreen *screen = app.primaryScreen()) {
        window.setGeometry(screen->geometry());
        qDebug() << "Framebuffer size:" << screen->geometry();
    } else {
        window.showFullScreen();
    }
    window.show();
    return app.exec();
}
