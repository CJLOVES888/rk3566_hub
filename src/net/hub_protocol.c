/*
 * ============================================================================
 * hub_protocol.c —— 标准消息 build / parse
 * ============================================================================
 */

#include "hub_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

void hub_protocol_make_id(char *out, size_t out_sz)
{
    unsigned char bytes[4] = {0};
    if (!out || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    if (out_sz < 9) {
        return;
    }

#if defined(_WIN32)
    /* 开发机兜底：时间混合即可 */
    {
        unsigned seed = (unsigned)time(NULL) ^ (unsigned)GetCurrentProcessId();
        for (int i = 0; i < 4; ++i) {
            seed = seed * 1103515245u + 12345u;
            bytes[i] = (unsigned char)(seed >> 16);
        }
    }
#else
    {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0) {
            if (read(fd, bytes, sizeof(bytes)) != (ssize_t)sizeof(bytes)) {
                memset(bytes, 0, sizeof(bytes));
            }
            close(fd);
        } else {
            unsigned seed = (unsigned)time(NULL) ^ (unsigned)getpid();
            for (int i = 0; i < 4; ++i) {
                seed = seed * 1103515245u + 12345u;
                bytes[i] = (unsigned char)(seed >> 16);
            }
        }
    }
#endif
    snprintf(out, out_sz, "%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3]);
}

int64_t hub_protocol_now_ts(void)
{
    return (int64_t)time(NULL);
}

int hub_protocol_build(const char *type, cJSON *payload, const char *id, char **out_json)
{
    char idbuf[16];
    cJSON *root = NULL;
    char *printed = NULL;

    if (!type || !out_json) {
        if (payload) {
            cJSON_Delete(payload);
        }
        return 0;
    }
    *out_json = NULL;

    if (!id || !id[0]) {
        hub_protocol_make_id(idbuf, sizeof(idbuf));
        id = idbuf;
    }
    if (!payload) {
        payload = cJSON_CreateObject();
        if (!payload) {
            return 0;
        }
    }

    root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(payload);
        return 0;
    }

    /* 本仓库 cJSON 的 Add*ToObject 为 void，不能当布尔用 */
    {
        cJSON *type_item = cJSON_CreateString(type);
        cJSON *id_item = cJSON_CreateString(id);
        cJSON *ts_item = cJSON_CreateNumber((double)hub_protocol_now_ts());
        if (!type_item || !id_item || !ts_item) {
            cJSON_Delete(type_item);
            cJSON_Delete(id_item);
            cJSON_Delete(ts_item);
            cJSON_Delete(root);
            cJSON_Delete(payload);
            return 0;
        }
        cJSON_AddItemToObject(root, "type", type_item);
        cJSON_AddItemToObject(root, "id", id_item);
        cJSON_AddItemToObject(root, "ts", ts_item);
    }
    cJSON_AddItemToObject(root, "payload", payload); /* 接管 payload */

    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return 0;
    }
    *out_json = printed;
    return 1;
}

int hub_protocol_parse(const char *raw, size_t len, cJSON **out)
{
    cJSON *root = NULL;
    if (!raw || !out) {
        return 0;
    }
    *out = NULL;
    (void)len;
    root = cJSON_Parse(raw);
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
        }
        return 0;
    }
    *out = root;
    return 1;
}

int hub_protocol_payload_action(const cJSON *root, char *out, size_t out_sz)
{
    const cJSON *payload = NULL;
    const cJSON *action = NULL;
    if (!root || !out || out_sz == 0) {
        return 0;
    }
    out[0] = '\0';
    payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsObject(payload)) {
        return 0;
    }
    action = cJSON_GetObjectItemCaseSensitive(payload, "action");
    if (!cJSON_IsString(action) || !action->valuestring) {
        return 0;
    }
    snprintf(out, out_sz, "%s", action->valuestring);
    return 1;
}
