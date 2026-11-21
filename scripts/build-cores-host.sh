#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT_DIR/third_party"
OUT_DIR="$ROOT_DIR/build/cores"

"$ROOT_DIR/scripts/prepare-cores-src.sh"
mkdir -p "$OUT_DIR"

echo "Building AppleWin (libretro, x86_64, external ROMs)..."
cmake -S "$SRC_DIR/AppleWin" -B "$ROOT_DIR/build/cores-applewin" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PROJECT_applewin_INCLUDE="$ROOT_DIR/cmake/applewin_no_roms/override.cmake" \
    -DBUILD_LIBRETRO=ON \
    -DBUILD_APPLEN=OFF \
    -DBUILD_SA2=OFF \
    -DBUILD_QAPPLE=OFF \
    -DENABLE_NETWORKING=OFF \
    -DSTATIC_LINKING=OFF
cmake --build "$ROOT_DIR/build/cores-applewin" --target applewin_libretro -j"$(nproc)"
cp -f "$ROOT_DIR/build/cores-applewin/source/frontends/libretro/applewin_libretro.so" "$OUT_DIR/"

for emu in x64sc x64; do
    echo "Building VICE $emu (libretro, x86_64, external ROMs)..."
    (
        cd "$SRC_DIR/vice-libretro"
        make clean >/dev/null 2>&1 || true
        CFLAGS="-I$ROOT_DIR/cmake/vice_no_roms -O3 -Wno-incompatible-pointer-types -Wno-implicit-function-declaration" \
        CXXFLAGS="-I$ROOT_DIR/cmake/vice_no_roms -O3" \
        make -j"$(nproc)" platform=unix EMUTYPE="$emu"
        cp -f "vice_${emu}_libretro.so" "$OUT_DIR/"
        make clean >/dev/null 2>&1 || true
    )
done

echo "Building QUASI88 (libretro, x86_64)..."
(
    cd "$SRC_DIR/quasi88-libretro"
    make clean >/dev/null 2>&1 || true
    CFLAGS="-O3 -Wno-incompatible-pointer-types -Wno-implicit-function-declaration" \
    CXXFLAGS="-O3" \
    make -j"$(nproc)" platform=unix
    cp -f "quasi88_libretro.so" "$OUT_DIR/"
    make clean >/dev/null 2>&1 || true
)

echo "Building NP2kai (libretro, x86_64)..."
(
    cd "$SRC_DIR/NP2kai/sdl"
    NP2KAI_HASH="$(git -C "$SRC_DIR/NP2kai" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    make -f Makefile.libretro clean >/dev/null 2>&1 || true
    CFLAGS="-O3 -Wno-incompatible-pointer-types -Wno-implicit-function-declaration" \
    CXXFLAGS="-O3" \
    make -f Makefile.libretro -j"$(nproc)" platform=unix \
        NP2KAI_VERSION=rev.22 NP2KAI_HASH="$NP2KAI_HASH"
    cp -f "np2kai_libretro.so" "$OUT_DIR/"
    make -f Makefile.libretro clean >/dev/null 2>&1 || true
)

echo "Built host cores in $OUT_DIR:"
ls -lh "$OUT_DIR"
