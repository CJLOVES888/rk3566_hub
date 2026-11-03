/*
 * ============================================================================
 * mqtt_client.cpp —— Qt 信号槽 ↔ hub_mqtt
 * ============================================================================
 */

#include "mqtt_client.h"
#include "hub_mqtt.h"

#include <QMetaObject>
#include <QByteArray>

#include <cstdio>
#include <cstring>

namespace net {

namespace {

void copyField(char *dst, size_t dst_len, const QString &src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    const QByteArray utf8 = src.toUtf8();
    std::snprintf(dst, dst_len, "%s", utf8.constData());
}

hub_mqtt_config_t toC(const MqttConfig &cfg)
{
    hub_mqtt_config_t c;
    hub_mqtt_config_default(&c);
    copyField(c.host, sizeof(c.host), cfg.host);
    c.port = cfg.port;
    copyField(c.client_id, sizeof(c.client_id), cfg.clientId);
    copyField(c.username, sizeof(c.username), cfg.username);
    copyField(c.password, sizeof(c.password), cfg.password);
    copyField(c.cmd_topic, sizeof(c.cmd_topic), cfg.cmdTopic);
    copyField(c.event_topic, sizeof(c.event_topic), cfg.eventTopic);
    copyField(c.data_topic, sizeof(c.data_topic), cfg.dataTopic);
    c.keepalive_sec = cfg.keepaliveSec;
    return c;
}

} // namespace

MqttClient::MqttClient(QObject *parent)
    : QObject(parent)
{
    hub_mqtt_lib_init();
}

MqttClient::~MqttClient()
{
    stop();
    hub_mqtt_lib_cleanup();
}

void MqttClient::setConfig(const MqttConfig &cfg)
{
    m_cfg = cfg;
}

bool MqttClient::start()
{
    if (m_c) {
        return true;
    }
    if (m_cfg.clientId.isEmpty()) {
        emit errorOccurred(QStringLiteral("MQTT clientId/UID 为空，请设置环境变量 HUB_MQTT_UID"));
        return false;
    }

    hub_mqtt_callbacks_t cbs;
    std::memset(&cbs, 0, sizeof(cbs));
    cbs.user = this;
    cbs.on_connected = &MqttClient::onConnectedC;
    cbs.on_disconnected = &MqttClient::onDisconnectedC;
    cbs.on_message = &MqttClient::onMessageC;
    cbs.on_error = &MqttClient::onErrorC;

    const hub_mqtt_config_t c = toC(m_cfg);
    m_c = hub_mqtt_create(&c, &cbs);
    if (!m_c) {
        emit errorOccurred(QStringLiteral("hub_mqtt_create 失败"));
        return false;
    }
    if (!hub_mqtt_start(static_cast<hub_mqtt_t *>(m_c))) {
        hub_mqtt_destroy(static_cast<hub_mqtt_t *>(m_c));
        m_c = nullptr;
        return false;
    }
    return true;
}

void MqttClient::stop()
{
    if (!m_c) {
        return;
    }
    hub_mqtt_destroy(static_cast<hub_mqtt_t *>(m_c));
    m_c = nullptr;
}

bool MqttClient::isConnected() const
{
    return hub_mqtt_is_connected(static_cast<const hub_mqtt_t *>(m_c)) != 0;
}

bool MqttClient::publish(const QString &topic, const QByteArray &payload, int qos)
{
    if (!m_c) {
        emit errorOccurred(QStringLiteral("MQTT 未连接，无法发布"));
        return false;
    }
    return hub_mqtt_publish(static_cast<hub_mqtt_t *>(m_c), topic.toUtf8().constData(),
                            payload.constData(), payload.size(), qos) != 0;
}

bool MqttClient::publishEvent(const QByteArray &payload, int qos)
{
    if (!m_c) {
        return false;
    }
    return hub_mqtt_publish_event(static_cast<hub_mqtt_t *>(m_c), payload.constData(),
                                  payload.size(), qos) != 0;
}

bool MqttClient::publishData(const QByteArray &payload, int qos)
{
    if (!m_c) {
        return false;
    }
    return hub_mqtt_publish_data(static_cast<hub_mqtt_t *>(m_c), payload.constData(),
                                 payload.size(), qos) != 0;
}

void MqttClient::onConnectedC(void *user)
{
    auto *self = static_cast<MqttClient *>(user);
    QMetaObject::invokeMethod(self, [self]() { emit self->connected(); }, Qt::QueuedConnection);
}

void MqttClient::onDisconnectedC(void *user, int /*rc*/)
{
    auto *self = static_cast<MqttClient *>(user);
    QMetaObject::invokeMethod(self, [self]() { emit self->disconnected(); }, Qt::QueuedConnection);
}

void MqttClient::onMessageC(void *user, const char *topic, const void *payload, int len)
{
    auto *self = static_cast<MqttClient *>(user);
    const QString t = QString::fromUtf8(topic ? topic : "");
    const QByteArray p(static_cast<const char *>(payload), len > 0 ? len : 0);
    QMetaObject::invokeMethod(
        self, [self, t, p]() { emit self->messageReceived(t, p); }, Qt::QueuedConnection);
}

void MqttClient::onErrorC(void *user, const char *message)
{
    auto *self = static_cast<MqttClient *>(user);
    const QString msg = QString::fromUtf8(message ? message : "");
    QMetaObject::invokeMethod(self, [self, msg]() { emit self->errorOccurred(msg); },
                              Qt::QueuedConnection);
}

} // namespace net
