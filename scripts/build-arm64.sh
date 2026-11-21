#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="$ROOT_DIR/dist-arm64"
IMAGE_NAME="czx-arm64-builder"

"$ROOT_DIR/scripts/prepare-cores-src.sh"
mkdir -p "$DIST_DIR/cores"

echo "Building ARM64 cross-compile Docker image ($IMAGE_NAME)..."
docker build -f "$ROOT_DIR/Dockerfile.arm64" -t "$IMAGE_NAME" "$ROOT_DIR"

echo "Cross-compiling czx and libretro cores for ARM64..."
docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$ROOT_DIR:/workspace" \
    "$IMAGE_NAME" \
    bash -lc '
        set -euo pipefail
        NPROC="$(nproc)"

        cmake -S /workspace -B /workspace/build-arm64 -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE=/workspace/cmake/toolchain-aarch64.cmake \
            -DCZX_TARGET_DEVICE=ON \
            -DCZX_BUILD_TESTS=OFF \
            -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++"
        cmake --build /workspace/build-arm64 -j"$NPROC"
        cp -f /workspace/build-arm64/czx /workspace/dist-arm64/czx

        cmake -S /workspace/third_party/AppleWin -B /workspace/build-arm64-applewin -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE=/workspace/cmake/toolchain-aarch64.cmake \
            -DCMAKE_PROJECT_applewin_INCLUDE=/workspace/cmake/applewin_no_roms/override.cmake \
            -DCMAKE_C_FLAGS="-mcpu=cortex-a53 -I/workspace/third_party/AppleWin/zlib" \
            -DBUILD_LIBRETRO=ON \
            -DBUILD_APPLEN=OFF \
            -DBUILD_SA2=OFF \
            -DBUILD_QAPPLE=OFF \
            -DENABLE_NETWORKING=OFF \
            -DSTATIC_LINKING=OFF \
            -DCMAKE_SHARED_LINKER_FLAGS="-static-libgcc -static-libstdc++"
        cmake --build /workspace/build-arm64-applewin --target applewin_libretro -j"$NPROC"
        cp -f /workspace/build-arm64-applewin/source/frontends/libretro/applewin_libretro.so /workspace/dist-arm64/cores/

        cd /workspace/third_party/vice-libretro
        for emu in x64sc x64; do
            make clean >/dev/null 2>&1 || true
            CFLAGS="-I/workspace/cmake/vice_no_roms -O3 -mcpu=cortex-a53 -Wno-incompatible-pointer-types -Wno-implicit-function-declaration" \
            CXXFLAGS="-I/workspace/cmake/vice_no_roms -O3 -mcpu=cortex-a53" \
            LDFLAGS="-static-libgcc -static-libstdc++" \
            make -j"$NPROC" platform=unix EMUTYPE="$emu" \
                CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ AR=aarch64-linux-gnu-ar
            cp -f "vice_${emu}_libretro.so" /workspace/dist-arm64/cores/
            make clean >/dev/null 2>&1 || true
        done
    '

cp -f "$ROOT_DIR/packaging/applaunch/czx.desktop" "$DIST_DIR/"
cp -f "$ROOT_DIR/packaging/applaunch/czx.png" "$DIST_DIR/"
cp -f "$ROOT_DIR/packaging/applaunch/run-czx" "$DIST_DIR/"
cp -f "$ROOT_DIR/packaging/applaunch/install-on-device.sh" "$DIST_DIR/"
chmod +x "$DIST_DIR/czx" "$DIST_DIR/run-czx" "$DIST_DIR/install-on-device.sh"

echo "ARM64 distribution staged in $DIST_DIR:"
ls -lh "$DIST_DIR" "$DIST_DIR/cores"
