#!/bin/bash
# 探测哪个 /dev/videoN 能真正出图（RK CSI 常用）
set -u

echo "==== devices ===="
ls -l /dev/video* 2>/dev/null || echo "no video nodes"

echo
echo "==== v4l2 caps (need: v4l-utils) ===="
if ! command -v v4l2-ctl >/dev/null 2>&1; then
  echo "install: sudo apt install -y v4l-utils"
else
  for d in /dev/video-camera0 /dev/video{0..15}; do
    [ -e "$d" ] || continue
    echo "----- $d -----"
    v4l2-ctl -d "$d" --info 2>/dev/null | head -20
    v4l2-ctl -d "$d" --list-formats-ext 2>/dev/null | head -40
  done
fi

echo
echo "==== quick OpenCV/GStreamer tip ===="
echo "export HUB_CAMERA_DEVICE=/dev/video-camera0   # or /dev/videoX that lists Video Capture"
echo "Then: bash scripts/run.sh"
