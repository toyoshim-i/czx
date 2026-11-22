# MSX / MSX2 / MSX2+ (`bluemsx`)

`czx` runs MSX / MSX2 / MSX2+ emulation via `blueMSX` (`bluemsx_libretro.so`). Open-source **C-BIOS** machines (`MSX - C-BIOS`, `MSX2 - C-BIOS`, `MSX2+ - C-BIOS`, 2-Clause BSD) and XML ROM databases are bundled for immediate cartridge playback, while adding real MSX BIOS and Disk ROMs unlocks MSX-BASIC and `.dsk` software.

← [Back to README](../README.md)

## Quick Facts

| Item | Value |
| :--- | :--- |
| Core ID | `bluemsx` |
| Binary | `bluemsx_libretro.so` |
| ROM / Data Directory | `~/czx/roms/bluemsx/` (`Machines/Shared Roms/` for real ROMs) |
| ROM Case Sensitivity | Case-insensitive lookup inside `Shared Roms/` for auto-detected BIOS/optional ROMs; explicit machine `config.ini` files use exact paths on Linux |
| External ROMs Required? | **Optional for cartridges** (bundled C-BIOS); **Base BIOS required for MSX-BASIC** (`MSX.rom` etc.); **+ Disk ROM required for `.dsk` disks** |
| Media Directory | `~/czx/media/msx/` |
| Supported Media | `.rom`, `.mx1`, `.mx2`, `.ri`, `.dsk`, `.cas`, `.m3u`, `.col`, `.sg`, `.sc`, `.sf`, `.zip` |
| Native Active Area | `256×212` (or `512×212`, PAR `1:2`), cropped from `272×240` / `544×240` overscan |

## ROM & Machine Setup

`install-on-device.sh` (and `czx` on first run via `/usr/share/APPLaunch/share/czx/bluemsx` or `bluemsx-data/` beside the binary) automatically seeds open-source `Databases/*.xml`, `Machines/* - C-BIOS/`, and ROM-less `Machines/*/config.ini` into `~/czx/roms/bluemsx/`.

Place real MSX BIOS and Disk ROMs into `~/czx/roms/bluemsx/Machines/Shared Roms/` (matched case-insensitively by `czx` when auto-generating machine profiles):

| Target Tier | Minimum ROMs in `Machines/Shared Roms/` | Optional Expansion ROMs Used If Present |
| :--- | :--- | :--- |
| **`MSX2+` / `CZX - MSX2+`** *(highest priority)* | `MSX2P.rom`, `MSX2PEXT.rom` | `PANASONICDISK.rom` (or `PHILIPSDISK.rom`), `MSX2PMUS.rom`, `XBASIC2.rom`, `KANJI.rom`, `MSXKANJI.rom`, `MOONSOUND.rom` |
| **`MSX2` / `CZX - MSX2`** | `MSX2.rom`, `MSX2EXT.rom` | `PHILIPSDISK.rom` (or `PANASONICDISK.rom`), `MSX2PMUS.rom`, `XBASIC2.rom`, `MOONSOUND.rom` |
| **`MSX` / `CZX - MSX`** | `MSX.rom` *(works alone without a Disk ROM)* | `PHILIPSDISK.rom` (or `PANASONICDISK.rom`) |
| **`MSX2+ - C-BIOS`** *(fallback)* | *(bundled — no user ROMs needed)* | Cartridges only (`cbios_*.rom` already installed) |

- **Automatic machine generation & selection**:
  - When all ROMs for stock `MSX2+`, `MSX2`, or `MSX` exist, `czx` boots that stock machine directly.
  - When only the base BIOS ROMs exist (for example, just `MSX.rom` alone, or `MSX2P.rom` + `MSX2PEXT.rom` without `MOONSOUND.rom`/`KANJI.rom`/Disk ROMs), `czx` automatically generates `CZX - MSX`, `CZX - MSX2`, or `CZX - MSX2+` containing **only the ROMs present on disk** (and enables FDC automatically when a Disk ROM is present).
  - If you instead populate a specific machine folder under `Machines/` (such as `Machines/MSX2+ - Panasonic FS-A1WSX/`) with all of its ROMs, `czx` detects and uses that complete machine automatically.
  - Otherwise `czx` falls back to `MSX2+ - C-BIOS`.

## Media Setup & Usage

- Put MSX ROM cartridges, disks, and cassettes in `~/czx/media/msx/`.
- Open `Alt + 8` → **Load Media** to launch `.rom` / `.mx1` / `.mx2` / `.dsk` / `.cas` / `.m3u` images.
- **Disk images (`.dsk` / `.m3u`)**: Neither C-BIOS nor a `CZX - MSX*` profile without a Disk ROM can read floppies. Attempting to load a `.dsk` or `.m3u` without a Disk BIOS ROM displays a clear error prompting you to place `PANASONICDISK.rom` or `PHILIPSDISK.rom` in `~/czx/roms/bluemsx/Machines/Shared Roms/`.

## Display & Controls

- **Borders (`Alt + 8` → Borders)**: `czx` crops blueMSX's `272×240` / `544×240` frame to the `256×212` / `512×212` active display by default; enable **Borders** to view overscan margins.
- **Joystick P1 / P2 (`Alt + 7`)**: `F/Z/X/C` = Directions, `Space` = Button A (`FIRE1`), `Enter` = Button B (`FIRE2`).

## Configuration (`~/czx/config/bluemsx.cfg`)

Defaults seeded by `czx`:

```ini
bluemsx_msxtype = CZX - MSX2+   # Auto-detected: MSX2+/CZX - MSX2+, MSX2/CZX - MSX2, MSX/CZX - MSX, complete Machines/MSX*, or MSX2+ - C-BIOS
bluemsx_vdp_synctype = 60Hz
bluemsx_ym2413_enable = enabled
bluemsx_overscan = disabled
bluemsx_auto_rewind_cas = ON
```

Set `bluemsx_msxtype` explicitly (for example `MSX2`, `CZX - MSX`, or `MSX2+ - C-BIOS`) to force a specific profile. When explicitly specified, `czx` pre-validates all ROMs required by that machine's `config.ini` before starting blueMSX and shows **ROM FILES REQUIRED** if any are missing or invalid.
