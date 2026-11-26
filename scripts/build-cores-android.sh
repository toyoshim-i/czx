#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$ROOT_DIR/third_party"
JNILIBS_DIR="$ROOT_DIR/android/app/src/main/jniLibs"
API_LEVEL=26

ABIS="arm64-v8a x86_64"
CORES="all"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --abis)
            ABIS="$2"
            shift 2
            ;;
        --cores)
            CORES="$2"
            shift 2
            ;;
        *)
            echo "Usage: $0 [--abis \"arm64-v8a x86_64\"] [--cores \"all|quasi88 np2kai ...\"]" >&2
            exit 1
            ;;
    esac
done

ABIS="${ABIS//,/ }"
CORES="${CORES//,/ }"

if [[ "$CORES" == "all" ]]; then
    CORES="quasi88 np2kai bluemsx dosbox_pure applewin vice emuz700 efm7 msx68k"
fi

resolve_ndk() {
    if [[ -n "${ANDROID_NDK_HOME:-}" && -d "$ANDROID_NDK_HOME" ]]; then
        echo "$ANDROID_NDK_HOME"
        return
    fi
    local sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
    if [[ -d "$sdk/ndk/27.3.13750724" ]]; then
        echo "$sdk/ndk/27.3.13750724"
        return
    fi
    local latest
    latest="$(ls -1d "$sdk/ndk"/* 2>/dev/null | sort -V | tail -n 1 || true)"
    if [[ -n "$latest" && -d "$latest" ]]; then
        echo "$latest"
        return
    fi
    echo "ERROR: Android NDK not found. Set ANDROID_NDK_HOME." >&2
    exit 1
}

NDK_DIR="$(resolve_ndk)"
TOOLCHAIN_BIN="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin"
AR_BIN="$TOOLCHAIN_BIN/llvm-ar"
STRIP_BIN="$TOOLCHAIN_BIN/llvm-strip"
CMAKE_BIN="$(command -v cmake)"
if [[ -d "$HOME/Android/Sdk/cmake/3.22.1/bin" ]]; then
    CMAKE_BIN="$HOME/Android/Sdk/cmake/3.22.1/bin/cmake"
fi
NPROC="$(nproc)"

"$ROOT_DIR/scripts/prepare-cores-src.sh"

should_build() {
    local target="$1"
    for c in $CORES; do
        if [[ "$c" == "$target" ]]; then
            return 0
        fi
    done
    return 1
}

build_for_abi() {
    local abi="$1"
    local cc_bin=""
    local cxx_bin=""
    local rust_target=""
    local cpu_flags=""

    case "$abi" in
        arm64-v8a)
            cc_bin="$TOOLCHAIN_BIN/aarch64-linux-android${API_LEVEL}-clang"
            cxx_bin="$TOOLCHAIN_BIN/aarch64-linux-android${API_LEVEL}-clang++"
            rust_target="aarch64-linux-android"
            cpu_flags="-DPAGESIZE=4096"
            ;;
        x86_64)
            cc_bin="$TOOLCHAIN_BIN/x86_64-linux-android${API_LEVEL}-clang"
            cxx_bin="$TOOLCHAIN_BIN/x86_64-linux-android${API_LEVEL}-clang++"
            rust_target="x86_64-linux-android"
            cpu_flags=""
            ;;
        *)
            echo "Unsupported ABI: $abi" >&2
            exit 1
            ;;
    esac

    local out_dir="$JNILIBS_DIR/$abi"
    mkdir -p "$out_dir"

    if should_build "quasi88"; then
        echo "[$abi] Building QUASI88..."
        (
            cd "$SRC_DIR/quasi88-libretro"
            make -f "$ROOT_DIR/cmake/quasi88_czx/Makefile.czx" clean >/dev/null 2>&1 || true
            CFLAGS="-O3 -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-unknown-warning-option" \
            CXXFLAGS="-O3 -Wno-unknown-warning-option" \
            LDFLAGS="-lm" \
            make -f "$ROOT_DIR/cmake/quasi88_czx/Makefile.czx" -j"$NPROC" platform=unix \
                CC="$cc_bin" CXX="$cxx_bin" AR="$AR_BIN"
            cp -f "quasi88_libretro.so" "$out_dir/libquasi88_libretro.so"
            "$STRIP_BIN" --strip-unneeded "$out_dir/libquasi88_libretro.so"
            make -f "$ROOT_DIR/cmake/quasi88_czx/Makefile.czx" clean >/dev/null 2>&1 || true
        )
    fi

    if should_build "np2kai"; then
        echo "[$abi] Building NP2kai..."
        (
            cd "$SRC_DIR/NP2kai/sdl"
            local np2_hash
            np2_hash="$(git -C "$SRC_DIR/NP2kai" rev-parse --short HEAD 2>/dev/null || echo unknown)"
            make -f Makefile.libretro clean >/dev/null 2>&1 || true
            local midi_obj="$ROOT_DIR/build-android-${abi}-np2kai_midi_bridge.o"
            "$cc_bin" -O3 -fPIC -D__LIBRETRO__ -I. -I.. -Ilibretro -Ilibretro/libretro-common/include \
                -c "$ROOT_DIR/cmake/np2kai_czx/midi_bridge.c" -o "$midi_obj"
            CFLAGS="-O3 -fsigned-char -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-unknown-warning-option" \
            CXXFLAGS="-O3 -fno-rtti -Wno-unknown-warning-option" \
            LDFLAGS="$midi_obj -Wl,--wrap=pccore_reset -Wl,--wrap=commng_create -lm -static-libstdc++" \
            make -f Makefile.libretro -j"$NPROC" platform=unix HAS_GCC=0 \
                CC="$cc_bin" CXX="$cxx_bin" AR="$AR_BIN" \
                NP2KAI_VERSION=rev.22 NP2KAI_HASH="$np2_hash"
            cp -f "np2kai_libretro.so" "$out_dir/libnp2kai_libretro.so"
            "$STRIP_BIN" --strip-unneeded "$out_dir/libnp2kai_libretro.so"
            rm -f "$midi_obj"
            make -f Makefile.libretro clean >/dev/null 2>&1 || true
        )
    fi

    if should_build "bluemsx"; then
        echo "[$abi] Building blueMSX..."
        (
            cd "$SRC_DIR/blueMSX-libretro"
            make -f Makefile.libretro clean >/dev/null 2>&1 || true
            make -f Makefile.libretro -j"$NPROC" platform=unix \
                CC="$cc_bin -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-unknown-warning-option" \
                CXX="$cxx_bin -Wno-unknown-warning-option" \
                AR="$AR_BIN" \
                OPTIMIZE="-O3" \
                LDFLAGS="-lm -static-libstdc++"
            cp -f "bluemsx_libretro.so" "$out_dir/libbluemsx_libretro.so"
            "$STRIP_BIN" --strip-unneeded "$out_dir/libbluemsx_libretro.so"
            make -f Makefile.libretro clean >/dev/null 2>&1 || true
        )
    fi

    if should_build "dosbox_pure"; then
        echo "[$abi] Building DOSBox Pure..."
        (
            cd "$SRC_DIR/dosbox-pure"
            make clean >/dev/null 2>&1 || true
            make -j"$NPROC" \
                CXX="$cxx_bin" \
                STRIP="$STRIP_BIN" \
                MAKE_CPUFLAGS="$cpu_flags" \
                LDLIBS="-lm -static-libstdc++" \
                LDFLAGS="-Wl,--gc-sections -fno-ident -O2 -shared"
            cp -f "dosbox_pure_libretro.so" "$out_dir/libdosbox_pure_libretro.so"
            "$STRIP_BIN" --strip-unneeded "$out_dir/libdosbox_pure_libretro.so"
            make clean >/dev/null 2>&1 || true
        )
    fi

    if should_build "applewin"; then
        echo "[$abi] Building AppleWin..."
        local aw_build="$ROOT_DIR/build-android-${abi}-applewin"
        "$CMAKE_BIN" -S "$SRC_DIR/AppleWin" -B "$aw_build" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE="$NDK_DIR/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI="$abi" \
            -DANDROID_PLATFORM="android-${API_LEVEL}" \
            -DANDROID_STL=c++_static \
            -DCMAKE_PROJECT_applewin_INCLUDE="$ROOT_DIR/cmake/applewin_no_roms/override.cmake" \
            -DCMAKE_C_FLAGS="-I$SRC_DIR/AppleWin/zlib" \
            -DBUILD_LIBRETRO=ON \
            -DBUILD_APPLEN=OFF \
            -DBUILD_SA2=OFF \
            -DBUILD_QAPPLE=OFF \
            -DENABLE_NETWORKING=OFF \
            -DSTATIC_LINKING=OFF
        "$CMAKE_BIN" --build "$aw_build" --target applewin_libretro -j"$NPROC"
        cp -f "$aw_build/source/frontends/libretro/applewin_libretro.so" "$out_dir/libapplewin_libretro.so"
        "$STRIP_BIN" --strip-unneeded "$out_dir/libapplewin_libretro.so"
    fi

    if should_build "vice"; then
        for emu in x64 x64sc; do
            echo "[$abi] Building VICE $emu..."
            (
                cd "$SRC_DIR/vice-libretro"
                make clean >/dev/null 2>&1 || true
                CFLAGS="-I$ROOT_DIR/cmake/vice_no_roms -O3 -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-unknown-warning-option" \
                CXXFLAGS="-I$ROOT_DIR/cmake/vice_no_roms -O3 -Wno-unknown-warning-option" \
                LDFLAGS="-lm -static-libstdc++" \
                make -j"$NPROC" platform=unix EMUTYPE="$emu" \
                    CC="$cc_bin" CXX="$cxx_bin" AR="$AR_BIN"
                cp -f "vice_${emu}_libretro.so" "$out_dir/libvice_${emu}_libretro.so"
                "$STRIP_BIN" --strip-unneeded "$out_dir/libvice_${emu}_libretro.so"
                make clean >/dev/null 2>&1 || true
            )
        done
    fi

    if should_build "emuz700"; then
        echo "[$abi] Building EmuZ-700..."
        make -f "$ROOT_DIR/cmake/emuz700_czx/Makefile.czx" -j"$NPROC" \
            ROOT_DIR="$ROOT_DIR" \
            OBJ_DIR="$ROOT_DIR/build-android-${abi}-emuz700" \
            OUT_SO="$out_dir/libemuz700_libretro.so" \
            CXX="$cxx_bin" \
            OPT_FLAGS="-O3 -Wno-register" \
            GNU_UNIQUE_FLAG="" \
            EXTRA_LIBS="-lm -static-libstdc++"
        "$STRIP_BIN" --strip-unneeded "$out_dir/libemuz700_libretro.so"
    fi

    if should_build "efm7"; then
        for fm7_spec in "FM7:efm7" "FM77AV:efm77av" "FM77AV40:efm77av40"; do
            local fm7_model="${fm7_spec%%:*}"
            local fm7_name="${fm7_spec#*:}"
            echo "[$abi] Building $fm7_name ($fm7_model)..."
            make -f "$ROOT_DIR/cmake/efm7_czx/Makefile.czx" -j"$NPROC" \
                FM7_MODEL="$fm7_model" \
                ROOT_DIR="$ROOT_DIR" \
                OBJ_DIR="$ROOT_DIR/build-android-${abi}-${fm7_name}" \
                OUT_SO="$out_dir/lib${fm7_name}_libretro.so" \
                CXX="$cxx_bin" \
                OPT_FLAGS="-O3 -Wno-register" \
                GNU_UNIQUE_FLAG="" \
                EXTRA_LIBS="-lm -static-libstdc++"
            "$STRIP_BIN" --strip-unneeded "$out_dir/lib${fm7_name}_libretro.so"
        done
    fi

    if should_build "msx68k"; then
        echo "[$abi] Building msx68k (Rust)..."
        local cargo_env_var="CARGO_TARGET_${rust_target^^}_LINKER"
        cargo_env_var="${cargo_env_var//-/_}"
        local cc_env_var="CC_${rust_target//-/_}"
        local ar_env_var="AR_${rust_target//-/_}"
        env \
            "$cargo_env_var=$cc_bin" \
            "$cc_env_var=$cc_bin" \
            "$ar_env_var=$AR_BIN" \
            cargo build --release --target "$rust_target" \
            --manifest-path "$ROOT_DIR/cmake/msx68k_czx/Cargo.toml" \
            --target-dir "$ROOT_DIR/build-android-msx68k"
        cp -f "$ROOT_DIR/build-android-msx68k/${rust_target}/release/libmsx68k_libretro.so" \
            "$out_dir/libmsx68k_libretro.so"
        "$STRIP_BIN" --strip-unneeded "$out_dir/libmsx68k_libretro.so"
    fi
}

for abi in $ABIS; do
    build_for_abi "$abi"
done

if should_build "bluemsx"; then
    "$ROOT_DIR/scripts/stage-bluemsx-data.sh" \
        "$SRC_DIR/blueMSX-libretro/system/bluemsx" \
        "$ROOT_DIR/android/app/src/main/assets/bluemsx-data"
fi

echo "Android cores staged in $JNILIBS_DIR:"
find "$JNILIBS_DIR" -name '*.so' | sort | xargs -r ls -lh
