#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT_DIR/third_party"

if [ ! -e "$SRC_DIR/AppleWin/.git" ] || [ ! -e "$SRC_DIR/vice-libretro/.git" ] || \
   [ ! -e "$SRC_DIR/quasi88-libretro/.git" ] || [ ! -e "$SRC_DIR/NP2kai/.git" ] || \
   [ ! -e "$SRC_DIR/blueMSX-libretro/.git" ] || [ ! -e "$SRC_DIR/takeda-cscp-emulators/.git" ] || \
   [ ! -e "$SRC_DIR/msx68k/.git" ]; then
    echo "Initializing third_party git submodules..."
    git -C "$ROOT_DIR" submodule update --init --depth 1 \
        third_party/AppleWin third_party/vice-libretro \
        third_party/quasi88-libretro third_party/NP2kai \
        third_party/blueMSX-libretro third_party/takeda-cscp-emulators \
        third_party/msx68k
fi

if [ ! -f "$SRC_DIR/msx68k/third_party/rhas/Cargo.toml" ] || \
   [ ! -f "$SRC_DIR/msx68k/third_party/rhlk/Cargo.toml" ]; then
    echo "Initializing msx68k nested submodules (rhas, rhlk)..."
    git -C "$SRC_DIR/msx68k" submodule update --init --depth 1 third_party/rhas third_party/rhlk
fi
