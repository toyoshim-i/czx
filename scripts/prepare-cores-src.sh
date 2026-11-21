#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT_DIR/third_party"

if [ ! -e "$SRC_DIR/AppleWin/.git" ] || [ ! -e "$SRC_DIR/vice-libretro/.git" ]; then
    echo "Initializing third_party git submodules..."
    git -C "$ROOT_DIR" submodule update --init --depth 1 third_party/AppleWin third_party/vice-libretro
fi
