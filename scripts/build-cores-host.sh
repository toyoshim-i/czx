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
    make -f "$ROOT_DIR/cmake/quasi88_czx/Makefile.czx" -j"$(nproc)" platform=unix
    cp -f "quasi88_libretro.so" "$OUT_DIR/"
    make -f "$ROOT_DIR/cmake/quasi88_czx/Makefile.czx" clean >/dev/null 2>&1 || true
)

echo "Building NP2kai (libretro, x86_64)..."
(
    cd "$SRC_DIR/NP2kai/sdl"
    NP2KAI_HASH="$(git -C "$SRC_DIR/NP2kai" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    make -f Makefile.libretro clean >/dev/null 2>&1 || true
    MIDI_OBJ="$ROOT_DIR/build/np2kai_midi_bridge.o"
    gcc -O3 -fPIC -D__LIBRETRO__ -I. -I.. -Ilibretro -Ilibretro/libretro-common/include \
        -c "$ROOT_DIR/cmake/np2kai_czx/midi_bridge.c" -o "$MIDI_OBJ"
    CFLAGS="-O3 -Wno-incompatible-pointer-types -Wno-implicit-function-declaration" \
    CXXFLAGS="-O3" \
    LDFLAGS="$MIDI_OBJ -Wl,--wrap=pccore_reset -Wl,--wrap=commng_create" \
    make -f Makefile.libretro -j"$(nproc)" platform=unix \
        NP2KAI_VERSION=rev.22 NP2KAI_HASH="$NP2KAI_HASH"
    cp -f "np2kai_libretro.so" "$OUT_DIR/"
    rm -f "$MIDI_OBJ"
    make -f Makefile.libretro clean >/dev/null 2>&1 || true
)

echo "Building blueMSX (libretro, x86_64)..."
(
    cd "$SRC_DIR/blueMSX-libretro"
    make -f Makefile.libretro clean >/dev/null 2>&1 || true
    make -f Makefile.libretro -j"$(nproc)" platform=unix \
        CC="gcc -Wno-incompatible-pointer-types -Wno-implicit-function-declaration" \
        OPTIMIZE="-O3"
    cp -f "bluemsx_libretro.so" "$OUT_DIR/"
    make -f Makefile.libretro clean >/dev/null 2>&1 || true
)
"$ROOT_DIR/scripts/stage-bluemsx-data.sh" \
    "$SRC_DIR/blueMSX-libretro/system/bluemsx" \
    "$ROOT_DIR/build/bluemsx-data"

echo "Building EmuZ-700 (libretro, x86_64, external ROMs)..."
make -f "$ROOT_DIR/cmake/emuz700_czx/Makefile.czx" -j"$(nproc)" \
    ROOT_DIR="$ROOT_DIR" \
    OBJ_DIR="$ROOT_DIR/build/cores-emuz700" \
    OUT_SO="$OUT_DIR/emuz700_libretro.so" \
    OPT_FLAGS="-O3"

echo "Building eFM-7 / eFM77AV / eFM77AV40 (libretro, x86_64, external ROMs)..."
for fm7_spec in "FM7:efm7" "FM77AV:efm77av" "FM77AV40:efm77av40"; do
    fm7_model="${fm7_spec%%:*}"
    fm7_name="${fm7_spec#*:}"
    make -f "$ROOT_DIR/cmake/efm7_czx/Makefile.czx" -j"$(nproc)" \
        FM7_MODEL="$fm7_model" \
        ROOT_DIR="$ROOT_DIR" \
        OBJ_DIR="$ROOT_DIR/build/cores-${fm7_name}" \
        OUT_SO="$OUT_DIR/${fm7_name}_libretro.so" \
        OPT_FLAGS="-O3"
done

if command -v cargo >/dev/null 2>&1; then
    echo "Building msx68k (experimental libretro, host)..."
    cargo build --release --manifest-path "$ROOT_DIR/cmake/msx68k_czx/Cargo.toml" \
        --target-dir "$ROOT_DIR/build/cores-msx68k"
    cp -f "$ROOT_DIR/build/cores-msx68k/release/libmsx68k_libretro.so" "$OUT_DIR/msx68k_libretro.so"
else
    echo "cargo not found; skipping experimental msx68k core."
fi

echo "Built host cores in $OUT_DIR:"
ls -lh "$OUT_DIR"
