/*
 * ============================================================================
 * hub_protocol.h —— 三端统一 JSON 消息（cJSON）
 * ----------------------------------------------------------------------------
 * 格式：
 *   {"type":"cmd|data|event|ack","id":"...","ts":1730000000,"payload":{...}}
 *
 * MQTT 主题约定（巴法云）：rk3566cmd / rk3566event
 * ============================================================================
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "storage/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 写入最多 8 个十六进制字符的短 id（不含花括号），保证 NUL 结尾 */
void hub_protocol_make_id(char *out, size_t out_sz);

int64_t hub_protocol_now_ts(void);

/**
 * 组装一条标准消息。payload 可为 NULL（空对象）；成功后由本函数接管并释放。
 * *out_json 需由调用方 free()。
 * 成功返回 1。
 */
int hub_protocol_build(const char *type, cJSON *payload, const char *id, char **out_json);

/**
 * 解析 JSON；失败返回 0。*out 需由调用方 cJSON_Delete。
 */
int hub_protocol_parse(const char *raw, size_t len, cJSON **out);

/** 从 root.payload.action 取字符串；无则返回 0 */
int hub_protocol_payload_action(const cJSON *root, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif
