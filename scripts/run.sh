#!/bin/bash
# ============================================================
# run.sh —— linuxfb 启动 + MQTT 环境检查
# ============================================================
# 用法：
#   export HUB_MQTT_UID=<巴法云私钥UID>
#   bash scripts/run.sh
# ============================================================

export QT_QPA_PLATFORM=linuxfb
export QT_QPA_FB=/dev/fb0
export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=/dev/input/event1
export QT_QPA_PLATFORM_PLUGIN_PATH=

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BINARY="$PROJECT_ROOT/build/bin/rk3566_hub"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: 找不到 $BINARY"
    echo "请先编译："
    echo "  sudo apt install -y libmosquitto-dev libqt5sql5-sqlite"
    echo "  cd $PROJECT_ROOT && mkdir -p build && cd build"
    echo "  cmake .. && make -j\$(nproc)"
    exit 1
fi

if [ -z "${HUB_MQTT_UID:-}" ]; then
    echo "WARN: 未设置 HUB_MQTT_UID，MQTT 不会连接（摄像头/SQLite 仍可用）"
    echo "      export HUB_MQTT_UID=你的巴法云私钥"
fi

# 模型默认：项目 models/yolov8n.rknn（可用 HUB_RKNN_MODEL 覆盖）
if [ -z "${HUB_RKNN_MODEL:-}" ]; then
    if [ -f "$PROJECT_ROOT/models/yolov8n.rknn" ]; then
        export HUB_RKNN_MODEL="$PROJECT_ROOT/models/yolov8n.rknn"
    fi
fi

# CSI 摄像头：优先系统软链接；仍失败时用 probe_camera.sh 查出正确节点再 export
if [ -z "${HUB_CAMERA_DEVICE:-}" ] && [ -e /dev/video-camera0 ]; then
    export HUB_CAMERA_DEVICE=/dev/video-camera0
fi

echo "============================================"
echo "  启动 rk3566_hub (linuxfb + MQTT + NPU)"
echo "============================================"
echo "  QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
echo "  HUB_MQTT_UID=${HUB_MQTT_UID:-<empty>}"
echo "  HUB_CAMERA_DEVICE=${HUB_CAMERA_DEVICE:-<auto>}"
echo "  HUB_RKNN_MODEL=${HUB_RKNN_MODEL:-<missing>}"
echo "============================================"

# 确保 SQLite 目录存在（可执行文件旁或上一级）
mkdir -p "$(dirname "$BINARY")/data"
mkdir -p "$PROJECT_ROOT/build/data"

exec "$BINARY" "$@"
