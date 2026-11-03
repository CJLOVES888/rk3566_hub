#!/bin/bash
# ============================================================
# run.sh —— 启动 rk3566_hub（自动加载 hub.env + 选择显示后端）
# ============================================================
# 用法：
#   bash scripts/run.sh
#   或安装一次后：hub
#
# 配置文件（按优先级加载第一个存在的）：
#   $HUB_ENV_FILE
#   <项目根>/hub.env
#   ~/.config/rk3566_hub/hub.env
#
# 显示说明：
#   - 桌面还在跑时，linuxfb 会被挡住；有 X0 / DISPLAY 时用 xcb
#   - 全屏：关桌面后在 hub.env 里设 HUB_FORCE_LINUXFB=1
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BINARY="$PROJECT_ROOT/build/bin/rk3566_hub"

# ---- 加载固化配置（不覆盖已在 shell 里 export 的变量）----
load_env_file() {
    local f="$1"
    [ -f "$f" ] || return 1
    # shellcheck disable=SC1090
    set -a
    # shellcheck source=/dev/null
    . "$f"
    set +a
    echo "NOTE: loaded env from $f"
    return 0
}

if [ -n "${HUB_ENV_FILE:-}" ]; then
    load_env_file "$HUB_ENV_FILE" || true
elif load_env_file "$PROJECT_ROOT/hub.env"; then
    :
elif load_env_file "$HOME/.config/rk3566_hub/hub.env"; then
    :
fi

if [ ! -f "$BINARY" ]; then
    echo "ERROR: 找不到 $BINARY"
    echo "请先编译："
    echo "  cd $PROJECT_ROOT && mkdir -p build && cd build"
    echo "  cmake .. && make -j\$(nproc)"
    exit 1
fi

# 串口登录常无 DISPLAY：若本机有 X0 且未指定，默认 :0（桌面调试）
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] \
    && [ "${HUB_FORCE_LINUXFB:-0}" != "1" ] \
    && [ -S /tmp/.X11-unix/X0 ]; then
    export DISPLAY=:0
    echo "NOTE: 检测到本机 X0，自动设置 DISPLAY=:0"
fi

# ---- 选择 Qt 显示平台 ----
if [ "${HUB_FORCE_LINUXFB:-0}" = "1" ]; then
    export QT_QPA_PLATFORM=linuxfb
    export QT_QPA_FB="${QT_QPA_FB:-/dev/fb0}"
elif [ -n "${DISPLAY:-}" ]; then
    export QT_QPA_PLATFORM="${HUB_QT_PLATFORM:-xcb}"
    if [ "$QT_QPA_PLATFORM" = "linuxfb" ]; then
        echo "NOTE: DISPLAY=$DISPLAY 且桌面可能在运行 → 改用 xcb（不要用 linuxfb）"
        export QT_QPA_PLATFORM=xcb
    fi
elif [ -n "${HUB_QT_PLATFORM:-}" ]; then
    export QT_QPA_PLATFORM="$HUB_QT_PLATFORM"
elif [ -n "${WAYLAND_DISPLAY:-}" ]; then
    export QT_QPA_PLATFORM=wayland
else
    export QT_QPA_PLATFORM=linuxfb
    export QT_QPA_FB="${QT_QPA_FB:-/dev/fb0}"
    export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS="${QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS:-/dev/input/event1}"
fi

if [ "$QT_QPA_PLATFORM" = "linuxfb" ]; then
    if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ] \
        || systemctl is-active --quiet graphical.target 2>/dev/null; then
        echo "WARN: 当前用 linuxfb，但系统图形桌面可能仍在运行。"
        echo "      调试请用桌面/DISPLAY=:0；全屏请先："
        echo "        sudo systemctl isolate multi-user.target"
    fi
fi

if [ -z "${HUB_MQTT_UID:-}" ]; then
    echo "WARN: 未设置 HUB_MQTT_UID，MQTT 不会连接"
    echo "      编辑 $PROJECT_ROOT/hub.env 写入 UID，或："
    echo "      bash $SCRIPT_DIR/install_board_env.sh <你的巴法云私钥>"
fi

if [ -z "${HUB_RKNN_MODEL:-}" ]; then
    if [ -f "$PROJECT_ROOT/models/yolov8n.rknn" ]; then
        export HUB_RKNN_MODEL="$PROJECT_ROOT/models/yolov8n.rknn"
    fi
fi

if [ -z "${HUB_CAMERA_DEVICE:-}" ] && [ -e /dev/video-camera0 ]; then
    export HUB_CAMERA_DEVICE=/dev/video-camera0
fi

echo "============================================"
echo "  启动 rk3566_hub (值守 + MQTT + 摄像头)"
echo "============================================"
echo "  QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
echo "  DISPLAY=${DISPLAY:-<empty>}  WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<empty>}"
if [ -n "${HUB_MQTT_UID:-}" ]; then
    echo "  HUB_MQTT_UID=<set ${#HUB_MQTT_UID} chars>"
else
    echo "  HUB_MQTT_UID=<empty>"
fi
echo "  HUB_CAMERA_DEVICE=${HUB_CAMERA_DEVICE:-<auto>}"
echo "  HUB_PRESENCE=${HUB_PRESENCE:-1}"
echo "  HUB_PRESENCE_IDLE_SEC=${HUB_PRESENCE_IDLE_SEC:-30}"
echo "  HUB_RKNN_MODEL=${HUB_RKNN_MODEL:-<missing>}"
echo "============================================"

mkdir -p "$(dirname "$BINARY")/data"
mkdir -p "$PROJECT_ROOT/build/data"

exec "$BINARY" "$@"
