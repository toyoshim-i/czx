# MSX / MSX2 / MSX2+ (`bluemsx`)

`czx` runs MSX / MSX2 / MSX2+ emulation via `blueMSX` (`bluemsx_libretro.so`). Open-source **C-BIOS** machines (`MSX - C-BIOS`, `MSX2 - C-BIOS`, `MSX2+ - C-BIOS`, 2-Clause BSD) and XML ROM databases are bundled for immediate cartridge playback, while adding real MSX BIOS and Disk ROMs unlocks MSX-BASIC and `.dsk` software.

← [Back to README](../README.md)

## Quick Facts

| Item | Value |
| :--- | :--- |
| Core ID | `bluemsx` |
| Binary | `bluemsx_libretro.so` |
| ROM / Data Directory | `~/czx/roms/bluemsx/` (`Machines/Shared Roms/` for real ROMs) |
| ROM Case Sensitivity | **Case-sensitive (filenames must match `Machines/*/config.ini` exactly)** |
| External ROMs Required? | **Optional for cartridges** (bundled C-BIOS); **Required for MSX-BASIC / `.dsk` disks** |
| Media Directory | `~/czx/media/msx/` |
| Supported Media | `.rom`, `.mx1`, `.mx2`, `.ri`, `.dsk`, `.cas`, `.m3u`, `.col`, `.sg`, `.sc`, `.sf`, `.zip` |
| Native Active Area | `256×212` (or `512×212`, PAR `1:2`), cropped from `272×240` / `544×240` overscan |

## ROM & Machine Setup

`install-on-device.sh` (and `czx` on first run via `/usr/share/APPLaunch/share/czx/bluemsx` or `bluemsx-data/` beside the binary) automatically seeds open-source `Databases/*.xml`, `Machines/* - C-BIOS/`, and ROM-less `Machines/*/config.ini` into `~/czx/roms/bluemsx/`.

Place real MSX BIOS and Disk ROMs into `~/czx/roms/bluemsx/Machines/Shared Roms/` using the **exact filenames** expected by blueMSX:

| Target Machine (Auto-Detected) | Required Trigger ROMs in `Machines/Shared Roms/` | Additional Referenced ROMs in `config.ini` |
| :--- | :--- | :--- |
| **`MSX2+`** *(checked first)* | `MSX2P.rom`, `MSX2PEXT.rom` | `PANASONICDISK.rom`, `MSX2PMUS.rom`, `XBASIC2.rom`, `KANJI.rom`, `MSXKANJI.rom`, `MOONSOUND.rom` |
| **`MSX2`** | `MSX2.rom`, `MSX2EXT.rom` | `PHILIPSDISK.rom`, `MSX2PMUS.rom`, `XBASIC2.rom`, `MOONSOUND.rom` |
| **`MSX`** | `MSX.rom` | `PHILIPSDISK.rom` |
| **`MSX2+ - C-BIOS`** *(fallback)* | *(bundled — no user ROMs needed)* | Cartridges only (`cbios_*.rom` already installed) |

- **Machine auto-selection**: Unless overridden in `~/czx/config/bluemsx.cfg`, `czx` selects the highest real machine profile whose trigger ROMs and `Machines/<Machine>/config.ini` exist, falling back to `MSX2+ - C-BIOS`.

## Media Setup & Usage

- Put MSX ROM cartridges, disks, and cassettes in `~/czx/media/msx/`.
- Open `Alt + 8` → **Load Media** to launch `.rom` / `.mx1` / `.mx2` / `.dsk` / `.cas` / `.m3u` images.
- **Disk images (`.dsk` / `.m3u`)**: C-BIOS does not include a disk BASIC / BDOS implementation. If you attempt to load a `.dsk` or `.m3u` while `bluemsx_msxtype` resolves to a C-BIOS profile, `czx` shows an error prompting you to place real MSX BIOS + Disk ROMs (`PANASONICDISK.rom` or `PHILIPSDISK.rom`) in `~/czx/roms/bluemsx/Machines/Shared Roms/`.

## Display & Controls

- **Borders (`Alt + 8` → Borders)**: `czx` crops blueMSX's `272×240` / `544×240` frame to the `256×212` / `512×212` active display by default; enable **Borders** to view overscan margins.
- **Joystick P1 / P2 (`Alt + 7`)**: `F/Z/X/C` = Directions, `Space` = Button A (`FIRE1`), `Enter` = Button B (`FIRE2`).

## Configuration (`~/czx/config/bluemsx.cfg`)

Defaults seeded by `czx`:

```ini
bluemsx_msxtype = MSX2+   # Auto-detected: MSX2+ / MSX2 / MSX / MSX2+ - C-BIOS
bluemsx_vdp_synctype = 60Hz
bluemsx_ym2413_enable = enabled
bluemsx_overscan = disabled
bluemsx_auto_rewind_cas = ON
```

Set `bluemsx_msxtype` explicitly (for example `MSX2`, `MSX`, or `MSX2+ - C-BIOS`) to lock the machine type instead of auto-detecting.
