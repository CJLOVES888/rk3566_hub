/*
 * ============================================================================
 * protocol.h —— 三端统一 JSON 消息格式（板端 / 手机 / PC）
 * ----------------------------------------------------------------------------
 * 格式：
 *   {"type":"cmd|data|event|ack","id":"...","ts":1730000000,"payload":{...}}
 *
 * MQTT 主题约定（巴法云 MQTT 设备云，主题须在控制台预先创建）：
 *   rk3566cmd    手机/PC → 板子（板子订阅）
 *   rk3566event  板子 → 手机/PC（板子发布）
 * Host: mqtt.bemfa.com  Port: 9501  Username: 私钥UID
 * ============================================================================
 */

#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QDateTime>

namespace net {

inline QString makeMsgId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

inline qint64 nowTs()
{
    return QDateTime::currentSecsSinceEpoch();
}

/** 组装一条标准消息并序列化为紧凑 JSON（无换行，便于 MQTT payload） */
inline QByteArray buildMessage(const QString &type,
                               const QJsonObject &payload,
                               const QString &id = QString())
{
    QJsonObject root;
    root.insert(QStringLiteral("type"), type);
    root.insert(QStringLiteral("id"), id.isEmpty() ? makeMsgId() : id);
    root.insert(QStringLiteral("ts"), nowTs());
    root.insert(QStringLiteral("payload"), payload);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

/** 解析 JSON；失败返回 false，ok 输出解析后的根对象 */
inline bool parseMessage(const QByteArray &raw, QJsonObject *out)
{
    if (!out) {
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    *out = doc.object();
    return true;
}

inline QString payloadAction(const QJsonObject &root)
{
    return root.value(QStringLiteral("payload")).toObject()
               .value(QStringLiteral("action")).toString();
}

} // namespace net
