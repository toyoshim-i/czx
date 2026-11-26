#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="$ROOT_DIR/dist-arm64"
MANIFEST="$ROOT_DIR/app-builder.json"

VERSION=""
NO_STRIP=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            VERSION="${2:?--version requires an argument}"
            shift 2
            ;;
        --no-strip)
            NO_STRIP=1
            shift
            ;;
        -h|--help)
            cat <<EOF
Usage: $0 [--version <ver>] [--no-strip]
Builds dist-arm64/<pkg>_<version>_arm64.deb from dist-arm64/ and packaging/.
EOF
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

for tool in python3 dpkg-deb file; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required tool '$tool' not found in PATH" >&2
        exit 1
    fi
done

if [ ! -f "$MANIFEST" ]; then
    echo "ERROR: $MANIFEST not found" >&2
    exit 1
fi

eval "$(python3 - "$MANIFEST" "$VERSION" <<'PYEOF'
import json, shlex, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
pkg = data["package_name"]
ver = sys.argv[2] or data["version"]
desc = data.get("description") or pkg
print(f"PKG_NAME={shlex.quote(pkg)}")
print(f"PKG_VERSION={shlex.quote(ver)}")
print(f"PKG_SHORT_DESC={shlex.quote(desc)}")
PYEOF
)"

if [ ! -f "$DIST_DIR/czx" ]; then
    echo "ERROR: $DIST_DIR/czx missing. Run ./scripts/build-arm64.sh first." >&2
    exit 1
fi

if ! file "$DIST_DIR/czx" | grep -q "ARM aarch64"; then
    echo "ERROR: $DIST_DIR/czx is not an ARM aarch64 binary" >&2
    file "$DIST_DIR/czx" >&2
    exit 1
fi

CORE_COUNT=0
for so in "$DIST_DIR"/cores/*.so; do
    [ -f "$so" ] || continue
    if ! file "$so" | grep -q "ARM aarch64"; then
        echo "ERROR: $so is not an ARM aarch64 shared library" >&2
        file "$so" >&2
        exit 1
    fi
    CORE_COUNT=$((CORE_COUNT + 1))
done
if [ "$CORE_COUNT" -eq 0 ]; then
    echo "ERROR: no *_libretro.so files found in $DIST_DIR/cores" >&2
    exit 1
fi

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/czx-deb-stage.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

PREFIX="usr/share/APPLaunch"
install -d \
    "$STAGE/DEBIAN" \
    "$STAGE/$PREFIX/bin" \
    "$STAGE/$PREFIX/applications" \
    "$STAGE/$PREFIX/share/images" \
    "$STAGE/$PREFIX/share/czx/cores" \
    "$STAGE/usr/bin" \
    "$STAGE/usr/share/doc/$PKG_NAME"

install -m 0755 "$DIST_DIR/czx" "$STAGE/$PREFIX/bin/czx"
install -m 0755 "$ROOT_DIR/packaging/applaunch/run-czx" "$STAGE/$PREFIX/bin/run-czx"
install -m 0644 "$ROOT_DIR/packaging/applaunch/czx.desktop" "$STAGE/$PREFIX/applications/czx.desktop"
install -m 0644 "$ROOT_DIR/packaging/applaunch/czx.png" "$STAGE/$PREFIX/share/images/czx.png"

for so in "$DIST_DIR"/cores/*.so; do
    [ -f "$so" ] || continue
    install -m 0755 "$so" "$STAGE/$PREFIX/share/czx/cores/$(basename "$so")"
done

if [ -d "$DIST_DIR/bluemsx-data" ]; then
    install -d "$STAGE/$PREFIX/share/czx/bluemsx"
    cp -a "$DIST_DIR/bluemsx-data/." "$STAGE/$PREFIX/share/czx/bluemsx/"
    BAD_ROMS="$(find "$STAGE/$PREFIX/share/czx/bluemsx" -type f \( -iname '*.rom' -o -iname '*.bin' \) ! -name 'cbios_*.rom')"
    if [ -n "$BAD_ROMS" ]; then
        echo "ERROR: non-C-BIOS ROM files found in bluemsx-data:" >&2
        echo "$BAD_ROMS" >&2
        exit 1
    fi
fi

ln -s "../share/APPLaunch/bin/run-czx" "$STAGE/usr/bin/czx"
install -m 0644 "$ROOT_DIR/packaging/store/copyright" "$STAGE/usr/share/doc/$PKG_NAME/copyright"
install -m 0644 "$ROOT_DIR/README.md" "$STAGE/usr/share/doc/$PKG_NAME/README.md"

if [ "$NO_STRIP" -eq 0 ]; then
    if command -v aarch64-linux-gnu-strip >/dev/null 2>&1; then
        aarch64-linux-gnu-strip --strip-unneeded \
            "$STAGE/$PREFIX/bin/czx" \
            "$STAGE/$PREFIX/share/czx/cores/"*.so
    elif command -v docker >/dev/null 2>&1 && docker image inspect czx-arm64-builder >/dev/null 2>&1; then
        docker run --rm --user "$(id -u):$(id -g)" -v "$STAGE:/stage" czx-arm64-builder \
            bash -lc 'aarch64-linux-gnu-strip --strip-unneeded /stage/usr/share/APPLaunch/bin/czx /stage/usr/share/APPLaunch/share/czx/cores/*.so'
    else
        echo "WARN: aarch64-linux-gnu-strip unavailable; packaging unstripped binaries" >&2
    fi
fi

chmod -R a-s "$STAGE"
INSTALLED_KB="$(du -sk --apparent-size "$STAGE/usr" | awk '{print $1}')"

cat >"$STAGE/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $PKG_VERSION
Architecture: arm64
Maintainer: Takashi Toyoshima <toyoshim@gmail.com>
Section: APPLaunch
Priority: optional
Homepage: https://github.com/toyoshim-i/czx
Installed-Size: $INSTALLED_KB
Depends: libc6
Recommends: libsdl2-2.0-0, libgles2, libegl1, libasound2
Description: $PKG_SHORT_DESC
 Lightweight C++20 retro computer multi-emulator platform for M5Stack
 Cardputer Zero (320x170 SPI LCD, TCA8418 I2C keyboard, and automatic
 fullscreen GLES2 HDMI output) supporting Apple II, Commodore 64,
 NEC PC-8801, NEC PC-9801, MSX/MSX2/MSX2+, SHARP PC-G850/MZ-700/X68000,
 Fujitsu FM-7/FM77AV/FM77AV40, and IBM PC / DOS. No copyrighted ROMs are bundled.
EOF

cat >"$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/postinst"

OUT_DEB="$DIST_DIR/${PKG_NAME}_${PKG_VERSION}_arm64.deb"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT_DEB"

dpkg-deb -I "$OUT_DEB"
CTRL_ARCH="$(dpkg-deb -f "$OUT_DEB" Architecture)"
if [ "$CTRL_ARCH" != "arm64" ]; then
    echo "ERROR: unexpected Architecture '$CTRL_ARCH'" >&2
    exit 1
fi
dpkg-deb -c "$OUT_DEB" > "$STAGE/contents.txt"
if ! grep -q 'usr/share/APPLaunch/applications/.*\.desktop$' "$STAGE/contents.txt"; then
    echo "ERROR: .desktop missing from usr/share/APPLaunch/applications/" >&2
    exit 1
fi

ls -lh "$OUT_DEB"
