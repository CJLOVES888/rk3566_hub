#!/bin/bash
# 在板子上安装 / 升级 librknnrt（与 toolkit 版本对齐）
# 用法：在已拷贝 rknpu2 的机器上执行，或把 so 放到同目录
set -euo pipefail

SRC="${1:-}"
if [ -z "$SRC" ]; then
  # 默认：仓库里的 aarch64 runtime
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  SRC="$SCRIPT_DIR/../../rknpu2-master/rknpu2-master/runtime/RK356X/Linux/librknn_api/aarch64/librknnrt.so"
fi

if [ ! -f "$SRC" ]; then
  echo "找不到 $SRC"
  echo "用法: bash install_rknn_runtime.sh /path/to/librknnrt.so"
  exit 1
fi

echo "Install $SRC -> /usr/lib/librknnrt.so"
sudo cp /usr/lib/librknnrt.so /usr/lib/librknnrt.so.bak 2>/dev/null || true
sudo cp "$SRC" /usr/lib/librknnrt.so
sudo ldconfig
echo "OK. version strings:"
strings /usr/lib/librknnrt.so | grep -i "librknnrt version" | head -3
