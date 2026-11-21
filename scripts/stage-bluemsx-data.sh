#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <src_bluemsx_system_dir> <dest_dir>" >&2
    exit 1
fi

SRC_DIR="$1"
DEST_DIR="$2"

if [ ! -d "$SRC_DIR/Databases" ] || [ ! -d "$SRC_DIR/Machines" ]; then
    echo "Invalid blueMSX system directory: $SRC_DIR" >&2
    exit 1
fi

rm -rf "$DEST_DIR/Databases" "$DEST_DIR/Machines"
mkdir -p "$DEST_DIR/Databases" "$DEST_DIR/Machines/Shared Roms"

for xml in "$SRC_DIR"/Databases/*.xml; do
    if [ -f "$xml" ]; then
        cp -p "$xml" "$DEST_DIR/Databases/"
    fi
done

for cbios in "MSX - C-BIOS" "MSX2 - C-BIOS" "MSX2+ - C-BIOS"; do
    if [ -d "$SRC_DIR/Machines/$cbios" ]; then
        cp -a "$SRC_DIR/Machines/$cbios" "$DEST_DIR/Machines/"
    fi
done

for dir in "$SRC_DIR"/Machines/*; do
    [ -d "$dir" ] || continue
    name="$(basename "$dir")"
    case "$name" in
        "Shared Roms"|*C-BIOS*)
            continue
            ;;
    esac
    if [ -f "$dir/config.ini" ]; then
        mkdir -p "$DEST_DIR/Machines/$name"
        cp -p "$dir/config.ini" "$DEST_DIR/Machines/$name/config.ini"
    fi
done

BAD_ROMS="$(find "$DEST_DIR" -type f \( -iname '*.rom' -o -iname '*.bin' \) ! -name 'cbios_*.rom')"
if [ -n "$BAD_ROMS" ]; then
    echo "ERROR: Non-C-BIOS ROM files found in staged bluemsx-data:" >&2
    echo "$BAD_ROMS" >&2
    exit 1
fi

echo "Staged open-source blueMSX data in $DEST_DIR"
