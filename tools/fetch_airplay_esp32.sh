#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TP_DIR="$ROOT_DIR/third_party"
TARGET="$TP_DIR/airplay-esp32"

mkdir -p "$TP_DIR"

if [ -d "$TARGET/.git" ]; then
  echo "third_party/airplay-esp32 already exists, updating..."
  git -C "$TARGET" fetch --all --tags
  git -C "$TARGET" pull --ff-only || true
else
  echo "Cloning rbouteiller/airplay-esp32 into third_party/airplay-esp32..."
  git clone https://github.com/rbouteiller/airplay-esp32.git "$TARGET"
fi

echo
echo "Current revision:"
git -C "$TARGET" rev-parse HEAD
git -C "$TARGET" describe --tags --always || true

echo
echo "Next: inspect docs/PORTING_AIRPLAY_ESP32.md"
