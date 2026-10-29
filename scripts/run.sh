#!/bin/bash
# ============================================================
# run.sh —— 启动 rk3566_hub（自动选择显示后端）
# ============================================================
# 用法：
#   export HUB_MQTT_UID=<巴法云私钥UID>
#   bash scripts/run.sh
#
# 显示说明（很重要）：
#   - 桌面还在跑时，linuxfb 画面会被桌面挡住，看起来像「只有系统界面」
#   - 默认：有 DISPLAY 就用 xcb（在桌面上开窗口，能看见）
#   - 全屏占屏：先关桌面，再 export HUB_QT_PLATFORM=linuxfb
#       sudo systemctl isolate multi-user.target
#       # 或：sudo systemctl stop lightdm / gdm3 / sddm
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BINARY="$PROJECT_ROOT/build/bin/rk3566_hub"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: 找不到 $BINARY"
    echo "请先编译："
    echo "  cd $PROJECT_ROOT && mkdir -p build && cd build"
    echo "  cmake .. && make -j\$(nproc)"
    exit 1
fi

# ---- 选择 Qt 显示平台 ----
# 有桌面 DISPLAY 时优先 xcb（避免 linuxfb 被 XFCE 挡住却「像在跑」）。
# 仅当 HUB_FORCE_LINUXFB=1 时才在有桌面时强行 linuxfb。
if [ "${HUB_FORCE_LINUXFB:-0}" = "1" ]; then
    export QT_QPA_PLATFORM=linuxfb
    export QT_QPA_FB="${QT_QPA_FB:-/dev/fb0}"
elif [ -n "${DISPLAY:-}" ]; then
    export QT_QPA_PLATFORM="${HUB_QT_PLATFORM:-xcb}"
    # 若误设 HUB_QT_PLATFORM=linuxfb 且桌面还在，强制改回 xcb
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

# linuxfb 且可能仍有图形桌面 → 提醒
if [ "$QT_QPA_PLATFORM" = "linuxfb" ]; then
    if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ] \
        || systemctl is-active --quiet graphical.target 2>/dev/null; then
        echo "WARN: 当前用 linuxfb，但系统图形桌面可能仍在运行。"
        echo "      若屏幕仍是桌面、看不到本程序，任选其一："
        echo "        1) 在桌面终端运行（推荐调试）："
        echo "             unset HUB_QT_PLATFORM; export DISPLAY=:0"
        echo "             bash $SCRIPT_DIR/run.sh"
        echo "        2) 关掉桌面后再用 linuxfb 全屏："
        echo "             sudo systemctl isolate multi-user.target"
        echo "             export HUB_QT_PLATFORM=linuxfb"
        echo "             bash $SCRIPT_DIR/run.sh"
    fi
fi

if [ -z "${HUB_MQTT_UID:-}" ]; then
    echo "WARN: 未设置 HUB_MQTT_UID，MQTT 不会连接（摄像头/SQLite 仍可用）"
    echo "      export HUB_MQTT_UID=你的巴法云私钥"
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
echo "  HUB_MQTT_UID=${HUB_MQTT_UID:-<empty>}"
echo "  HUB_CAMERA_DEVICE=${HUB_CAMERA_DEVICE:-<auto>}"
echo "  HUB_PRESENCE=${HUB_PRESENCE:-1}"
echo "  HUB_PRESENCE_IDLE_SEC=${HUB_PRESENCE_IDLE_SEC:-30}"
echo "  HUB_RKNN_MODEL=${HUB_RKNN_MODEL:-<missing>}"
echo "============================================"

mkdir -p "$(dirname "$BINARY")/data"
mkdir -p "$PROJECT_ROOT/build/data"

exec "$BINARY" "$@"
