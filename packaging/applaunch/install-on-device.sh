#!/bin/sh
set -eu
DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Installing czx to /usr/share/APPLaunch..."
sudo install -d /usr/share/APPLaunch/bin /usr/share/APPLaunch/applications \
    /usr/share/APPLaunch/share/images /usr/share/APPLaunch/share/czx/cores

sudo install -m 755 "$DIR/czx" /usr/share/APPLaunch/bin/czx
sudo install -m 755 "$DIR/run-czx" /usr/share/APPLaunch/bin/run-czx
sudo install -m 644 "$DIR/czx.desktop" /usr/share/APPLaunch/applications/czx.desktop
sudo install -m 644 "$DIR/czx.png" /usr/share/APPLaunch/share/images/czx.png

for so in "$DIR"/cores/*.so; do
    if [ -f "$so" ]; then
        sudo install -m 755 "$so" "/usr/share/APPLaunch/share/czx/cores/$(basename "$so")"
    fi
done

if [ -d "$DIR/bluemsx-data" ]; then
    sudo rm -rf /usr/share/APPLaunch/share/czx/bluemsx
    sudo install -d /usr/share/APPLaunch/share/czx/bluemsx
    sudo cp -a "$DIR/bluemsx-data/." /usr/share/APPLaunch/share/czx/bluemsx/
fi

DATA_DIR="${CZX_DATA_DIR:-$HOME/czx}"
mkdir -p "$DATA_DIR/roms/apple2" "$DATA_DIR/roms/vice/C64" "$DATA_DIR/roms/vice/DRIVES" \
    "$DATA_DIR/roms/quasi88" "$DATA_DIR/roms/np2kai" \
    "$DATA_DIR/roms/bluemsx/Machines/Shared Roms" "$DATA_DIR/roms/bluemsx/Databases" \
    "$DATA_DIR/roms/g800" \
    "$DATA_DIR/media/apple2" "$DATA_DIR/media/c64" \
    "$DATA_DIR/media/pc88" "$DATA_DIR/media/pc98" "$DATA_DIR/media/msx" "$DATA_DIR/media/g800" \
    "$DATA_DIR/saves" "$DATA_DIR/config"

if [ -d "$DIR/bluemsx-data" ]; then
    (
        cd "$DIR/bluemsx-data"
        find . -type d -exec mkdir -p "$DATA_DIR/roms/bluemsx/{}" \;
        find . -type f -exec sh -c '[ -e "$2/$1" ] || cp -p "$1" "$2/$1"' _ {} "$DATA_DIR/roms/bluemsx" \;
    )
fi

if command -v systemctl >/dev/null 2>&1; then
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
        systemctl --user restart APPLaunch.service || true
fi

echo "Done. Put media files in $DATA_DIR/media/{apple2,c64,pc88,pc98,msx,g800}."
