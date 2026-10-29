/*
 * ============================================================================
 * main_window.cpp —— 摄像头 + MQTT + SQLite + NPU 检测联动
 * ============================================================================
 */

#include "main_window.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDateTime>
#include <QTimer>
#include <QPixmap>
#include <QPainter>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStackedLayout>
#include <QMetaType>
#include <algorithm>

#include "vision/camera_worker.h"
#include "vision/presence_guard.h"
#include "net/mqtt_client.h"
#include "net/protocol.h"
#include "storage/database.h"

namespace ui {

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    qRegisterMetaType<vision::DetectBox>("vision::DetectBox");
    qRegisterMetaType<QVector<vision::DetectBox>>("QVector<vision::DetectBox>");

    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QWidget { background-color: #101010; color: #e0e0e0; }"
        "QPushButton {"
        "  background-color: #1e3a5f; color: #e0e0e0;"
        "  border: 1px solid #3a6ea5; border-radius: 6px;"
        "  padding: 10px 24px; font-size: 22px;"
        "}"
        "QPushButton:pressed { background-color: #2a5080; }"));
    setWindowTitle(QStringLiteral("RK3566 Smart Hub"));

    initStorage();
    buildUi();
    startPresence();
    startCamera();
    startDetector();
    startMqtt();

    auto *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, &MainWindow::onClockTick);
    clockTimer->start(1000);
    onClockTick();

    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 7 * 24 * 3600;
    storage::Database::instance().purgeEventsOlderThan(cutoff);
}

MainWindow::~MainWindow()
{
    shutdownMqtt();
    shutdownDetector();
    shutdownCamera();
    storage::Database::instance().close();
}

void MainWindow::initStorage()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("data/smart_hub.db")),
        QDir(appDir).filePath(QStringLiteral("../data/smart_hub.db")),
    };

    bool ok = false;
    for (const QString &path : candidates) {
        if (storage::Database::instance().open(QDir::cleanPath(path))) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        qWarning() << "SQLite 打开失败";
    }

    m_deviceId = storage::Database::instance().getConfig(
        QStringLiteral("device_id"), QStringLiteral("dev01"));
}

void MainWindow::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 12, 20, 12);
    layout->setSpacing(8);

    m_titleLabel = new QLabel(QStringLiteral("RK3566 书桌/门口值守"), this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 36px; font-weight: bold; color: #00d4ff;"));
    layout->addWidget(m_titleLabel);

    auto *stackHost = new QWidget(this);
    auto *stack = new QStackedLayout(stackHost);
    stack->setContentsMargins(0, 0, 0, 0);

    m_videoLabel = new QLabel(stackHost);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumSize(1, 1);
    m_videoLabel->setStyleSheet(QStringLiteral(
        "background-color: #000000; border: 1px solid #303030;"));
    stack->addWidget(m_videoLabel);

    m_overlayLabel = new QLabel(QStringLiteral("屏幕已关闭\n(无人超时 / 远程关屏)"), stackHost);
    m_overlayLabel->setAlignment(Qt::AlignCenter);
    m_overlayLabel->setStyleSheet(QStringLiteral(
        "background-color: #000000; color: #666666; font-size: 28px;"
        "border: 1px solid #303030;"));
    stack->addWidget(m_overlayLabel);
    stack->setCurrentWidget(m_videoLabel);
    layout->addWidget(stackHost, 1);

    m_statusLabel = new QLabel(QStringLiteral("摄像头启动中..."), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 20px; color: #aaaaaa;"));
    layout->addWidget(m_statusLabel);

    m_mqttLabel = new QLabel(QStringLiteral("MQTT: 未连接"), this);
    m_mqttLabel->setAlignment(Qt::AlignCenter);
    m_mqttLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #888888;"));
    layout->addWidget(m_mqttLabel);

    m_presenceLabel = new QLabel(QStringLiteral("值守: 初始化..."), this);
    m_presenceLabel->setAlignment(Qt::AlignCenter);
    m_presenceLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #888888;"));
    layout->addWidget(m_presenceLabel);

    m_npuLabel = new QLabel(QStringLiteral("NPU: 未启动"), this);
    m_npuLabel->setAlignment(Qt::AlignCenter);
    m_npuLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #888888;"));
    layout->addWidget(m_npuLabel);

    m_publishBtn = new QPushButton(QStringLiteral("上报测试事件"), this);
    connect(m_publishBtn, &QPushButton::clicked, this, &MainWindow::onPublishTestClicked);
    layout->addWidget(m_publishBtn, 0, Qt::AlignHCenter);

    m_clockLabel = new QLabel(this);
    m_clockLabel->setAlignment(Qt::AlignCenter);
    m_clockLabel->setStyleSheet(QStringLiteral("font-size: 20px; color: #888888;"));
    layout->addWidget(m_clockLabel);
}

QString MainWindow::resolveModelPath() const
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString fromEnv = env.value(QStringLiteral("HUB_RKNN_MODEL")).trimmed();
    if (!fromEnv.isEmpty() && QFileInfo::exists(fromEnv)) {
        return fromEnv;
    }

    const QString fromDb = storage::Database::instance()
                               .getConfig(QStringLiteral("rknn_model_path"))
                               .trimmed();
    if (!fromDb.isEmpty() && QFileInfo::exists(fromDb)) {
        return fromDb;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("../models/yolov8n.rknn")),
        QDir(appDir).filePath(QStringLiteral("models/yolov8n.rknn")),
        QDir(appDir).filePath(QStringLiteral("../../models/yolov8n.rknn")),
        QStringLiteral("/home/kickpi/rk3566_hub/models/yolov8n.rknn"),
    };
    for (const QString &p : candidates) {
        const QString clean = QDir::cleanPath(p);
        if (QFileInfo::exists(clean)) {
            return clean;
        }
    }
    return QString();
}

void MainWindow::startPresence()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    auto &db = storage::Database::instance();

    vision::PresenceConfig cfg;
    const QString en = env.value(QStringLiteral("HUB_PRESENCE"),
                                 db.getConfig(QStringLiteral("presence_enable"),
                                              QStringLiteral("1")));
    cfg.enabled = !(en == QLatin1String("0") || en.compare(QLatin1String("false"),
                                                           Qt::CaseInsensitive) == 0);

    cfg.idleMs = env.value(QStringLiteral("HUB_PRESENCE_IDLE_SEC"),
                           db.getConfig(QStringLiteral("presence_idle_sec"),
                                        QStringLiteral("30")))
                     .toInt()
                 * 1000;
    cfg.motionRatio = env.value(QStringLiteral("HUB_PRESENCE_THRESH"),
                                db.getConfig(QStringLiteral("presence_thresh"),
                                             QStringLiteral("0.025")))
                          .toDouble();
    cfg.confirmHits = env.value(QStringLiteral("HUB_PRESENCE_CONFIRM"),
                                db.getConfig(QStringLiteral("presence_confirm"),
                                             QStringLiteral("3")))
                          .toInt();

    m_presence = new vision::PresenceGuard(cfg, this);
    connect(m_presence, &vision::PresenceGuard::presenceNear,
            this, &MainWindow::onPresenceNear);
    connect(m_presence, &vision::PresenceGuard::presenceAway,
            this, &MainWindow::onPresenceAway);
    connect(m_presence, &vision::PresenceGuard::statusText,
            this, &MainWindow::onPresenceStatus);

    m_autoScreenFromPresence = cfg.enabled;
    if (cfg.enabled) {
        m_presenceLabel->setText(
            QStringLiteral("值守: 监测中 | 静止 %1s 息屏 | 阈值 %2%")
                .arg(cfg.idleMs / 1000)
                .arg(cfg.motionRatio * 100.0, 0, 'f', 1));
        m_presenceLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #66bb6a;"));
        qInfo() << "PresenceGuard ON idleMs=" << cfg.idleMs
                << "thresh=" << cfg.motionRatio
                << "confirm=" << cfg.confirmHits;
    } else {
        m_presenceLabel->setText(QStringLiteral("值守: 已关闭 (HUB_PRESENCE=0)"));
        m_presenceLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #888888;"));
        qInfo() << "PresenceGuard OFF";
    }
}

void MainWindow::startCamera()
{
    // preferredIndex=-1：自动探测 CSI/UVC（可用 HUB_CAMERA_DEVICE 指定）
    m_cameraWorker = new vision::CameraWorker(/*preferredIndex=*/-1, 640, 480);
    m_cameraWorker->moveToThread(&m_cameraThread);

    connect(&m_cameraThread, &QThread::started,
            m_cameraWorker, &vision::CameraWorker::start);
    connect(m_cameraWorker, &vision::CameraWorker::frameReady,
            this, &MainWindow::onFrame);
    connect(m_cameraWorker, &vision::CameraWorker::fpsChanged,
            this, &MainWindow::onFps);
    connect(m_cameraWorker, &vision::CameraWorker::errorOccurred,
            this, &MainWindow::onCameraError);

    m_cameraThread.start();
}

void MainWindow::shutdownCamera()
{
    if (!m_cameraWorker) {
        return;
    }
    m_cameraWorker->stop();
    m_cameraThread.quit();
    m_cameraThread.wait(3000);
    delete m_cameraWorker;
    m_cameraWorker = nullptr;
}

void MainWindow::startDetector()
{
    const QString modelPath = resolveModelPath();
    if (modelPath.isEmpty()) {
        m_npuLabel->setText(
            QStringLiteral("NPU: 未找到 yolov8n.rknn（放 models/ 或设 HUB_RKNN_MODEL）"));
        m_npuLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #cc8844;"));
        storage::Database::instance().logEvent(
            QStringLiteral("npu_skip"),
            QStringLiteral("{\"reason\":\"model_missing\"}"),
            QStringLiteral("npu"));
        return;
    }

    float conf = storage::Database::instance()
                     .getConfig(QStringLiteral("npu_conf_threshold"), QStringLiteral("0.5"))
                     .toFloat();
    if (conf <= 0.f || conf >= 1.f) {
        conf = 0.5f;
    }

    m_detector = new vision::Detector(modelPath, conf);
    m_detector->moveToThread(&m_detectorThread);

    connect(&m_detectorThread, &QThread::started,
            m_detector, &vision::Detector::start);
    connect(m_cameraWorker, &vision::CameraWorker::frameReady,
            m_detector, &vision::Detector::onFrame);
    connect(m_detector, &vision::Detector::detectionsUpdated,
            this, &MainWindow::onDetections);
    connect(m_detector, &vision::Detector::personDetected,
            this, &MainWindow::onPersonDetected);
    connect(m_detector, &vision::Detector::statusChanged,
            this, &MainWindow::onNpuStatus);
    connect(m_detector, &vision::Detector::errorOccurred,
            this, &MainWindow::onNpuError);

    m_npuLabel->setText(QStringLiteral("NPU: 启动中... %1").arg(modelPath));
    m_detectorThread.start();
}

void MainWindow::shutdownDetector()
{
    if (!m_detector) {
        return;
    }
    m_detector->stop();
    m_detectorThread.quit();
    m_detectorThread.wait(5000);
    delete m_detector;
    m_detector = nullptr;
}

void MainWindow::startMqtt()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString uid = env.value(QStringLiteral("HUB_MQTT_UID")).trimmed();
    if (uid.isEmpty()) {
        uid = storage::Database::instance()
                  .getConfig(QStringLiteral("mqtt_uid"))
                  .trimmed();
    }

    auto &db = storage::Database::instance();
    // 巴法云 MQTT 设备云：Host 用 mqtt.bemfa.com；主题必须是控制台已创建的 Topic（无斜杠）
    const QString host = env.value(QStringLiteral("HUB_MQTT_HOST"),
                                   db.getConfig(QStringLiteral("mqtt_host"),
                                                QStringLiteral("mqtt.bemfa.com")));
    const int port = env.value(QStringLiteral("HUB_MQTT_PORT"),
                               db.getConfig(QStringLiteral("mqtt_port"),
                                            QStringLiteral("9501")))
                           .toInt();
    const QString cmdTopic = env.value(
        QStringLiteral("HUB_MQTT_CMD_TOPIC"),
        db.getConfig(QStringLiteral("mqtt_cmd_topic"), QStringLiteral("rk3566cmd")));
    const QString eventTopic = env.value(
        QStringLiteral("HUB_MQTT_EVENT_TOPIC"),
        db.getConfig(QStringLiteral("mqtt_event_topic"), QStringLiteral("rk3566event")));

    net::MqttConfig cfg;
    cfg.host = host;
    cfg.port = port > 0 ? port : 9501;
    // 巴法云官方方式一：ClientId = 用户私钥 UID；用户名/密码可空。
    // （方式二才是 ClientId 随意 + Username=appID + Password=secretKey）
    // 手机若也用 UID 当 ClientId 会与板子互踢；手机请改用方式二，或只用网页发消息。
    cfg.clientId = env.value(QStringLiteral("HUB_MQTT_CLIENT_ID"), uid);
    cfg.username = env.value(QStringLiteral("HUB_MQTT_USERNAME")); // 默认空
    cfg.password = env.value(QStringLiteral("HUB_MQTT_PASSWORD"));
    cfg.cmdTopic = cmdTopic;       // 板子订阅：手机 → 板子
    cfg.eventTopic = eventTopic;    // 板子发布：板子 → 手机
    cfg.dataTopic = eventTopic;     // 巴法云可复用 event 主题
    qInfo() << "MQTT clientId=" << cfg.clientId
            << "user=" << (cfg.username.isEmpty() ? QStringLiteral("<empty>") : cfg.username)
            << "cmd=" << cfg.cmdTopic
            << "event=" << cfg.eventTopic;

    m_mqtt = new net::MqttClient(this);
    m_mqtt->setConfig(cfg);

    connect(m_mqtt, &net::MqttClient::connected, this, &MainWindow::onMqttConnected);
    connect(m_mqtt, &net::MqttClient::disconnected, this, &MainWindow::onMqttDisconnected);
    connect(m_mqtt, &net::MqttClient::messageReceived, this, &MainWindow::onMqttMessage);
    connect(m_mqtt, &net::MqttClient::errorOccurred, this, &MainWindow::onMqttError);

    if (uid.isEmpty()) {
        m_mqttLabel->setText(QStringLiteral("MQTT: 未配置 UID（export HUB_MQTT_UID=...）"));
        m_mqttLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #cc8844;"));
        return;
    }

    m_mqttLabel->setText(QStringLiteral("MQTT: 连接中..."));
    m_mqtt->start();
}

void MainWindow::shutdownMqtt()
{
    if (m_mqtt) {
        m_mqtt->stop();
    }
}

void MainWindow::refreshMqttStatusLabel()
{
    if (!m_mqtt) {
        return;
    }
    if (m_mqtt->isConnected()) {
        m_mqttLabel->setText(
            QStringLiteral("MQTT: 已连接 | %1").arg(m_mqtt->config().cmdTopic));
        m_mqttLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #66bb6a;"));
    } else {
        m_mqttLabel->setText(QStringLiteral("MQTT: 已断开，重连中..."));
        m_mqttLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #cc8844;"));
    }
}

void MainWindow::setScreenOn(bool on)
{
    m_screenOn = on;
    auto *stack = qobject_cast<QStackedLayout *>(m_videoLabel->parentWidget()->layout());
    if (!stack) {
        return;
    }
    stack->setCurrentWidget(on ? static_cast<QWidget *>(m_videoLabel)
                               : static_cast<QWidget *>(m_overlayLabel));
}

void MainWindow::handleCommand(const QString &action, const QByteArray &raw)
{
    QJsonObject root;
    QString cmdId;
    if (net::parseMessage(raw, &root)) {
        cmdId = root.value(QStringLiteral("id")).toString();
    }

    if (action == QLatin1String("screen_on")) {
        setScreenOn(true);
    } else if (action == QLatin1String("screen_off")) {
        setScreenOn(false);
    } else if (action == QLatin1String("presence_on")
               || action == QLatin1String("auto_on")) {
        m_autoScreenFromPresence = true;
        if (m_presence) {
            m_presence->setEnabled(true);
        }
    } else if (action == QLatin1String("presence_off")
               || action == QLatin1String("auto_off")) {
        m_autoScreenFromPresence = false;
        if (m_presence) {
            m_presence->setEnabled(false);
        }
    } else if (action == QLatin1String("query_status")) {
        /* ack 带状态 */
    } else if (action.isEmpty()) {
        return;
    }

    storage::Database::instance().logEvent(
        action.isEmpty() ? QStringLiteral("mqtt_cmd") : action,
        QString::fromUtf8(raw),
        QStringLiteral("mqtt"));
    logAndPublishAck(action, cmdId);
}

void MainWindow::logAndPublishAck(const QString &action, const QString &cmdId)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("action"), QStringLiteral("ack"));
    payload.insert(QStringLiteral("for"), action);
    payload.insert(QStringLiteral("screen_on"), m_screenOn);
    payload.insert(QStringLiteral("presence"),
                   m_presence && m_presence->isOccupied());
    payload.insert(QStringLiteral("auto_presence"), m_autoScreenFromPresence);
    payload.insert(QStringLiteral("fps"), m_lastFps);
    payload.insert(QStringLiteral("device_id"), m_deviceId);

    const QByteArray msg = net::buildMessage(
        QStringLiteral("ack"), payload, cmdId.isEmpty() ? net::makeMsgId() : cmdId);
    if (m_mqtt && m_mqtt->isConnected()) {
        m_mqtt->publishEvent(msg, 1);
    }
    storage::Database::instance().logEvent(
        QStringLiteral("ack"), QString::fromUtf8(msg), QStringLiteral("mqtt"));
}

QImage MainWindow::drawBoxes(const QImage &frame) const
{
    if (m_lastBoxes.isEmpty()) {
        return frame;
    }
    QImage out = frame.convertToFormat(QImage::Format_RGB888);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(0, 212, 255), 3);
    p.setPen(pen);
    QFont font = p.font();
    font.setPixelSize(18);
    p.setFont(font);

    for (const vision::DetectBox &b : m_lastBoxes) {
        p.drawRect(QRect(QPoint(b.x1, b.y1), QPoint(b.x2, b.y2)));
        const QString tag = (b.classId == 0)
                                ? QStringLiteral("person %.2f").arg(b.conf)
                                : QStringLiteral("id%1 %.2f").arg(b.classId).arg(b.conf);
        p.drawText(b.x1 + 4, std::max(20, b.y1 - 6), tag);
    }
    return out;
}

void MainWindow::onFrame(const QImage &frame)
{
    // 息屏时仍要把帧送给值守检测，否则无法「来人亮屏」
    if (m_presence) {
        m_presence->onFrame(frame);
    }
    if (!m_screenOn) {
        return;
    }
    const QImage shown = drawBoxes(frame);
    m_videoLabel->setPixmap(QPixmap::fromImage(shown).scaled(
        m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::publishPresenceEvent(const QString &action, double scoreOrIdle)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("action"), action);
    payload.insert(QStringLiteral("device_id"), m_deviceId);
    payload.insert(QStringLiteral("screen_on"), m_screenOn);
    if (action == QLatin1String("person_near")) {
        payload.insert(QStringLiteral("score"), scoreOrIdle);
    } else {
        payload.insert(QStringLiteral("idle_sec"), scoreOrIdle);
    }
    const QByteArray msg = net::buildMessage(QStringLiteral("event"), payload);
    storage::Database::instance().logEvent(
        action, QString::fromUtf8(msg), QStringLiteral("presence"));
    if (m_mqtt && m_mqtt->isConnected()) {
        m_mqtt->publishEvent(msg, 1);
    }
    qInfo() << "presence event" << action << scoreOrIdle;
}

void MainWindow::onPresenceNear(double score)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 防抖：3 秒内不重复推送
    if (now - m_lastPresenceEventMs < 3000) {
        if (m_autoScreenFromPresence) {
            setScreenOn(true);
        }
        return;
    }
    m_lastPresenceEventMs = now;

    if (m_autoScreenFromPresence) {
        setScreenOn(true);
    }
    m_titleLabel->setText(QStringLiteral("有人靠近"));
    QTimer::singleShot(2500, this, [this]() {
        m_titleLabel->setText(QStringLiteral("RK3566 书桌/门口值守"));
    });
    publishPresenceEvent(QStringLiteral("person_near"), score);
}

void MainWindow::onPresenceAway(double idleSec)
{
    m_lastPresenceEventMs = QDateTime::currentMSecsSinceEpoch();
    if (m_autoScreenFromPresence) {
        setScreenOn(false);
        m_overlayLabel->setText(
            QStringLiteral("屏幕已关闭\n(无人约 %1 秒)").arg(idleSec, 0, 'f', 0));
    }
    publishPresenceEvent(QStringLiteral("person_away"), idleSec);
}

void MainWindow::onPresenceStatus(const QString &text)
{
    if (!m_presenceLabel) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 状态字每 500ms 刷新一次，避免刷屏
    if (now - m_lastPresenceStatusMs < 500 && !text.contains(QStringLiteral("有人"))
        && !text.contains(QStringLiteral("息屏"))) {
        return;
    }
    m_lastPresenceStatusMs = now;
    m_presenceLabel->setText(text);
    m_presenceLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #66bb6a;"));
}

void MainWindow::onFps(double fps)
{
    m_lastFps = fps;
    m_statusLabel->setText(
        QStringLiteral("摄像头就绪 | 640x480 | FPS: %1").arg(fps, 0, 'f', 1));
}

void MainWindow::onCameraError(const QString &message)
{
    m_statusLabel->setText(message);
    storage::Database::instance().logEvent(
        QStringLiteral("camera_error"), message, QStringLiteral("camera"));
}

void MainWindow::onDetections(const QVector<vision::DetectBox> &boxes)
{
    m_lastBoxes = boxes;
}

void MainWindow::onPersonDetected(float conf)
{
    setScreenOn(true);
    m_titleLabel->setText(QStringLiteral("欢迎回家 (NPU)"));
    QTimer::singleShot(3000, this, [this]() {
        m_titleLabel->setText(QStringLiteral("RK3566 书桌/门口值守"));
    });

    QJsonObject payload;
    payload.insert(QStringLiteral("action"), QStringLiteral("person_detected"));
    payload.insert(QStringLiteral("conf"), conf);
    payload.insert(QStringLiteral("device_id"), m_deviceId);
    const QByteArray msg = net::buildMessage(QStringLiteral("event"), payload);

    storage::Database::instance().logEvent(
        QStringLiteral("person_detected"),
        QString::fromUtf8(msg),
        QStringLiteral("npu"));

    if (m_mqtt && m_mqtt->isConnected()) {
        m_mqtt->publishEvent(msg, 1);
    }
    qInfo() << "person_detected conf=" << conf;
}

void MainWindow::onNpuStatus(const QString &text)
{
    m_npuLabel->setText(text);
    m_npuLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #66bb6a;"));
}

void MainWindow::onNpuError(const QString &message)
{
    m_npuLabel->setText(QStringLiteral("NPU: %1").arg(message));
    m_npuLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #e57373;"));
    storage::Database::instance().logEvent(
        QStringLiteral("npu_error"), message, QStringLiteral("npu"));
}

void MainWindow::onMqttConnected()
{
    refreshMqttStatusLabel();
    storage::Database::instance().logEvent(
        QStringLiteral("mqtt_connected"),
        m_mqtt ? m_mqtt->config().cmdTopic : QString(),
        QStringLiteral("mqtt"));

    QJsonObject payload;
    payload.insert(QStringLiteral("action"), QStringLiteral("status_report"));
    payload.insert(QStringLiteral("online"), true);
    payload.insert(QStringLiteral("device_id"), m_deviceId);
    if (m_mqtt) {
        m_mqtt->publishEvent(net::buildMessage(QStringLiteral("event"), payload));
    }
}

void MainWindow::onMqttDisconnected()
{
    refreshMqttStatusLabel();
    storage::Database::instance().logEvent(
        QStringLiteral("mqtt_disconnected"), QString(), QStringLiteral("mqtt"));
}

void MainWindow::onMqttMessage(const QString &topic, const QByteArray &payload)
{
    qInfo() << "MQTT <<" << topic << payload;
    QString action;
    QJsonObject root;
    if (net::parseMessage(payload, &root)) {
        action = net::payloadAction(root);
        if (action.isEmpty()) {
            action = root.value(QStringLiteral("action")).toString();
        }
    } else {
        action = QString::fromUtf8(payload).trimmed();
    }
    handleCommand(action, payload);
}

void MainWindow::onMqttError(const QString &message)
{
    qWarning() << "MQTT error:" << message;
    m_mqttLabel->setText(QStringLiteral("MQTT: %1").arg(message));
    m_mqttLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #e57373;"));
    storage::Database::instance().logEvent(
        QStringLiteral("mqtt_error"), message, QStringLiteral("mqtt"));
}

void MainWindow::onPublishTestClicked()
{
    QJsonObject payload;
    payload.insert(QStringLiteral("action"), QStringLiteral("hello"));
    payload.insert(QStringLiteral("device_id"), m_deviceId);
    payload.insert(QStringLiteral("fps"), m_lastFps);
    payload.insert(QStringLiteral("screen_on"), m_screenOn);
    payload.insert(QStringLiteral("presence"),
                   m_presence && m_presence->isOccupied());
    const QByteArray msg = net::buildMessage(QStringLiteral("event"), payload);
    storage::Database::instance().logEvent(
        QStringLiteral("hello"), QString::fromUtf8(msg), QStringLiteral("ui"));

    if (!m_mqtt || !m_mqtt->isConnected()) {
        m_mqttLabel->setText(QStringLiteral("MQTT: 未连接，事件已写入本地 DB"));
        return;
    }
    if (m_mqtt->publishEvent(msg)) {
        m_mqttLabel->setText(QStringLiteral("MQTT: 已发布测试事件"));
        m_mqttLabel->setStyleSheet(QStringLiteral("font-size: 18px; color: #66bb6a;"));
    }
}

void MainWindow::onClockTick()
{
    m_clockLabel->setText(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

} // namespace ui
