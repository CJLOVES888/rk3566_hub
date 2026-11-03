/*
 * ============================================================================
 * hub_mqtt.h —— libmosquitto 封装（巴法云 / 通用 Broker）
 * ----------------------------------------------------------------------------
 * 网络循环在 mosquitto_loop_start 内部线程；回调在该线程触发。
 * Qt 侧通过 hub_mqtt_set_callbacks 注册，再自行切回 UI 线程。
 * ============================================================================
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hub_mqtt hub_mqtt_t;

typedef struct hub_mqtt_config {
    char host[128];
    int port;
    char client_id[128];
    char username[128];
    char password[128];
    char cmd_topic[128];
    char event_topic[128];
    char data_topic[128];
    int keepalive_sec;
} hub_mqtt_config_t;

typedef struct hub_mqtt_callbacks {
    void *user;
    void (*on_connected)(void *user);
    void (*on_disconnected)(void *user, int rc);
    void (*on_message)(void *user, const char *topic, const void *payload, int payload_len);
    void (*on_error)(void *user, const char *message);
} hub_mqtt_callbacks_t;

void hub_mqtt_config_default(hub_mqtt_config_t *cfg);

/** 进程级 init/cleanup（可多次调用，成对使用） */
void hub_mqtt_lib_init(void);
void hub_mqtt_lib_cleanup(void);

hub_mqtt_t *hub_mqtt_create(const hub_mqtt_config_t *cfg, const hub_mqtt_callbacks_t *cbs);
void hub_mqtt_destroy(hub_mqtt_t *m);

/** 异步连接 + 启动 loop 线程；成功返回 1 */
int hub_mqtt_start(hub_mqtt_t *m);
void hub_mqtt_stop(hub_mqtt_t *m);

int hub_mqtt_is_connected(const hub_mqtt_t *m);

int hub_mqtt_publish(hub_mqtt_t *m, const char *topic, const void *payload, int payload_len,
                     int qos);
int hub_mqtt_publish_event(hub_mqtt_t *m, const void *payload, int payload_len, int qos);
int hub_mqtt_publish_data(hub_mqtt_t *m, const void *payload, int payload_len, int qos);

const hub_mqtt_config_t *hub_mqtt_config(const hub_mqtt_t *m);

#ifdef __cplusplus
}
#endif
