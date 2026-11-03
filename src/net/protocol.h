/*
 * ============================================================================
 * protocol.h —— Qt 封装：内部调用 hub_protocol（cJSON）
 * ----------------------------------------------------------------------------
 * UI 仍可用 QJsonObject 拼 payload；序列化/解析走 hub_protocol。
 * ============================================================================
 */

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace net {

QString makeMsgId();

qint64 nowTs();

/** 组装一条标准消息并序列化为紧凑 JSON */
QByteArray buildMessage(const QString &type,
                        const QJsonObject &payload,
                        const QString &id = QString());

/** 解析 JSON；失败返回 false */
bool parseMessage(const QByteArray &raw, QJsonObject *out);

QString payloadAction(const QJsonObject &root);

} // namespace net
