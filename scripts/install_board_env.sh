#!/bin/bash
# ============================================================
# install_board_env.sh —— 板子上一键固化环境（只跑一次）
# ============================================================
#   bash scripts/install_board_env.sh
#   bash scripts/install_board_env.sh <巴法云UID>
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
ENV_FILE="$PROJECT_ROOT/hub.env"
EXAMPLE="$PROJECT_ROOT/hub.env.example"
UID_ARG="${1:-}"

if [ ! -f "$EXAMPLE" ]; then
    echo "ERROR: 缺少 $EXAMPLE"
    exit 1
fi

if [ ! -f "$ENV_FILE" ]; then
    cp "$EXAMPLE" "$ENV_FILE"
    echo "已创建 $ENV_FILE"
else
    echo "已存在 $ENV_FILE（不会覆盖）"
fi

if [ -n "$UID_ARG" ]; then
    if grep -q '^export HUB_MQTT_UID=' "$ENV_FILE"; then
        sed -i "s|^export HUB_MQTT_UID=.*|export HUB_MQTT_UID=${UID_ARG}|" "$ENV_FILE"
    else
        echo "export HUB_MQTT_UID=${UID_ARG}" >> "$ENV_FILE"
    fi
    echo "已写入 HUB_MQTT_UID"
fi

# ~/.bashrc 增加 alias，登录后可直接敲 hub
MARKER="# rk3566_hub alias"
if ! grep -qF "$MARKER" "$HOME/.bashrc" 2>/dev/null; then
    cat >> "$HOME/.bashrc" <<EOF

$MARKER
alias hub='bash $PROJECT_ROOT/scripts/run.sh'
EOF
    echo "已写入 ~/.bashrc：以后可直接输入 hub 启动"
else
    echo "~/.bashrc 已有 hub alias，跳过"
fi

chmod +x "$SCRIPT_DIR/run.sh" 2>/dev/null || true

echo
echo "完成。编辑密钥（如尚未填写）："
echo "  nano $ENV_FILE"
echo
echo "启动方式任选其一："
echo "  bash $SCRIPT_DIR/run.sh"
echo "  source ~/.bashrc && hub"
echo
