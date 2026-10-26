/*
 * ============================================================================
 * mqtt_client.cpp —— libmosquitto 实现
 * ============================================================================
 */

#include "mqtt_client.h"

#include <QMetaObject>
#include <QDebug>

#include <mosquitto.h>

namespace net {

MqttClient::MqttClient(QObject *parent)
    : QObject(parent)
{
    // 进程级初始化一次即可；多次调用是安全的（引用计数）
    mosquitto_lib_init();
}

MqttClient::~MqttClient()
{
    stop();
    mosquitto_lib_cleanup();
}

void MqttClient::setConfig(const MqttConfig &cfg)
{
    m_cfg = cfg;
}

bool MqttClient::start()
{
    if (m_mosq) {
        return true;
    }
    if (m_cfg.clientId.isEmpty()) {
        emit errorOccurred(QStringLiteral("MQTT clientId/UID 为空，请设置环境变量 HUB_MQTT_UID"));
        return false;
    }

    m_mosq = mosquitto_new(m_cfg.clientId.toUtf8().constData(),
                           /*clean_session=*/true,
                           /*userdata=*/this);
    if (!m_mosq) {
        emit errorOccurred(QStringLiteral("mosquitto_new 失败"));
        return false;
    }

    mosquitto_connect_callback_set(m_mosq, &MqttClient::onConnectThunk);
    mosquitto_disconnect_callback_set(m_mosq, &MqttClient::onDisconnectThunk);
    mosquitto_message_callback_set(m_mosq, &MqttClient::onMessageThunk);
    mosquitto_log_callback_set(m_mosq, &MqttClient::onLogThunk);

    // 断线后 2s 起指数退避重连，上限 30s
    mosquitto_reconnect_delay_set(m_mosq, 2, 30, true);

    // 巴法云方式一：ClientId=UID 时用户名密码可空；方式二才需要 appID/secretKey
    if (!m_cfg.username.isEmpty() || !m_cfg.password.isEmpty()) {
        const int authRc = mosquitto_username_pw_set(
            m_mosq,
            m_cfg.username.isEmpty() ? "" : m_cfg.username.toUtf8().constData(),
            m_cfg.password.isEmpty() ? nullptr : m_cfg.password.toUtf8().constData());
        if (authRc != MOSQ_ERR_SUCCESS) {
            emit errorOccurred(QStringLiteral("设置 MQTT 用户名失败: %1")
                                   .arg(QString::fromUtf8(mosquitto_strerror(authRc))));
            stop();
            return false;
        }
    }

    const int rc = mosquitto_connect_async(
        m_mosq,
        m_cfg.host.toUtf8().constData(),
        m_cfg.port,
        m_cfg.keepaliveSec);
    if (rc != MOSQ_ERR_SUCCESS) {
        emit errorOccurred(QStringLiteral("MQTT connect_async 失败: %1")
                               .arg(QString::fromUtf8(mosquitto_strerror(rc))));
        stop();
        return false;
    }

    const int loopRc = mosquitto_loop_start(m_mosq);
    if (loopRc != MOSQ_ERR_SUCCESS) {
        emit errorOccurred(QStringLiteral("MQTT loop_start 失败: %1")
                               .arg(QString::fromUtf8(mosquitto_strerror(loopRc))));
        stop();
        return false;
    }
    m_loopStarted = true;

    qInfo() << "MQTT connecting to" << m_cfg.host << m_cfg.port
            << "cmd=" << m_cfg.cmdTopic;
    return true;
}

void MqttClient::stop()
{
    if (!m_mosq) {
        return;
    }
    if (m_loopStarted) {
        mosquitto_loop_stop(m_mosq, /*force=*/true);
        m_loopStarted = false;
    }
    mosquitto_disconnect(m_mosq);
    mosquitto_destroy(m_mosq);
    m_mosq = nullptr;
    m_connected = false;
}

bool MqttClient::publish(const QString &topic, const QByteArray &payload, int qos)
{
    if (!m_mosq || !m_connected) {
        emit errorOccurred(QStringLiteral("MQTT 未连接，无法发布"));
        return false;
    }
    if (topic.isEmpty()) {
        return false;
    }

    const int rc = mosquitto_publish(
        m_mosq,
        nullptr,
        topic.toUtf8().constData(),
        payload.size(),
        payload.constData(),
        qos,
        /*retain=*/false);
    if (rc != MOSQ_ERR_SUCCESS) {
        emit errorOccurred(QStringLiteral("MQTT publish 失败: %1")
                               .arg(QString::fromUtf8(mosquitto_strerror(rc))));
        return false;
    }
    return true;
}

bool MqttClient::publishEvent(const QByteArray &payload, int qos)
{
    return publish(m_cfg.eventTopic, payload, qos);
}

bool MqttClient::publishData(const QByteArray &payload, int qos)
{
    return publish(m_cfg.dataTopic, payload, qos);
}

void MqttClient::onConnectThunk(struct mosquitto * /*mosq*/, void *obj, int rc)
{
    auto *self = static_cast<MqttClient *>(obj);
    QMetaObject::invokeMethod(self, [self, rc]() { self->handleConnect(rc); },
                              Qt::QueuedConnection);
}

void MqttClient::onDisconnectThunk(struct mosquitto * /*mosq*/, void *obj, int rc)
{
    auto *self = static_cast<MqttClient *>(obj);
    QMetaObject::invokeMethod(self, [self, rc]() { self->handleDisconnect(rc); },
                              Qt::QueuedConnection);
}

void MqttClient::onMessageThunk(struct mosquitto * /*mosq*/, void *obj,
                                const struct ::mosquitto_message *msg)
{
    auto *self = static_cast<MqttClient *>(obj);
    if (!msg || !msg->topic) {
        return;
    }
    const QString topic = QString::fromUtf8(msg->topic);
    const QByteArray payload(
        static_cast<const char *>(msg->payload),
        msg->payloadlen);
    QMetaObject::invokeMethod(self,
                              [self, topic, payload]() {
                                  self->handleMessage(topic, payload);
                              },
                              Qt::QueuedConnection);
}

void MqttClient::onLogThunk(struct mosquitto * /*mosq*/, void * /*obj*/, int level,
                            const char *str)
{
    // 只打 WARNING 及以上，避免刷屏；连接失败时这里最有用
    if (level <= MOSQ_LOG_WARNING && str) {
        qWarning() << "mosquitto:" << str;
    }
}

void MqttClient::handleConnect(int rc)
{
    if (rc != 0) {
        m_connected = false;
        const char *reason = mosquitto_connack_string(rc);
        qWarning() << "MQTT CONNACK failed rc=" << rc << reason;
        emit errorOccurred(QStringLiteral("MQTT 连接被拒绝 rc=%1 (%2)")
                               .arg(rc)
                               .arg(QString::fromUtf8(reason ? reason : "")));
        return;
    }
    m_connected = true;
    qInfo() << "MQTT connected OK, subscribing" << m_cfg.cmdTopic;

    if (!m_cfg.cmdTopic.isEmpty()) {
        const int subRc = mosquitto_subscribe(
            m_mosq, nullptr, m_cfg.cmdTopic.toUtf8().constData(), /*qos=*/1);
        if (subRc != MOSQ_ERR_SUCCESS) {
            emit errorOccurred(QStringLiteral("订阅 cmd 主题失败: %1")
                                   .arg(QString::fromUtf8(mosquitto_strerror(subRc))));
        } else {
            qInfo() << "MQTT subscribed:" << m_cfg.cmdTopic;
        }
    }

    emit connected();
}

void MqttClient::handleDisconnect(int rc)
{
    const bool was = m_connected;
    m_connected = false;
    qWarning() << "MQTT disconnected rc=" << rc;
    if (was) {
        emit disconnected();
    }
}

void MqttClient::handleMessage(const QString &topic, const QByteArray &payload)
{
    emit messageReceived(topic, payload);
}

} // namespace net
