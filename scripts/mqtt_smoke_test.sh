#!/bin/bash
# 巴法云命令行冒烟：主题须与控制台一致（rk3566cmd / rk3566event）
set -euo pipefail

UID_KEY="${HUB_MQTT_UID:-}"
HOST="${HUB_MQTT_HOST:-mqtt.bemfa.com}"
PORT="${HUB_MQTT_PORT:-9501}"
CMD_TOPIC="${HUB_MQTT_CMD_TOPIC:-rk3566cmd}"
EVENT_TOPIC="${HUB_MQTT_EVENT_TOPIC:-rk3566event}"

if [ -z "$UID_KEY" ]; then
    echo "请先: export HUB_MQTT_UID=你的巴法云私钥"
    exit 1
fi

echo "Host=$HOST:$PORT"
echo "ClientId(=UID)=$UID_KEY"
echo "订阅: $EVENT_TOPIC + $CMD_TOPIC"
echo "另开终端发布示例（ClientId 必须用私钥，不要和本 sub 同时抢同一个 -i）:"
echo "  mosquitto_pub -h $HOST -p $PORT -i smoke_pub_\$\$ -u appID -P secretKey -t $CMD_TOPIC -m screen_off"
echo "  # 或网页控制台直接往 rk3566cmd 发 screen_off"
echo

# 巴法云方式一：-i 私钥；用户名密码可省略
mosquitto_sub -h "$HOST" -p "$PORT" -i "$UID_KEY" -t "$EVENT_TOPIC" -t "$CMD_TOPIC" -v
