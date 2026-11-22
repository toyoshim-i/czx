# czx — Retro Computer Multi-Emulator Platform for Cardputer Zero

`czx` is a lightweight C++20 multi-emulator platform built for the **M5Stack Cardputer Zero** (`320×170` RGB565 SPI LCD, TCA8418 I2C keyboard, Raspberry Pi CM0 ARM64 Debian), with an X11/SDL2 desktop mode (native resolution or `320×170` LCD simulator) for Linux PCs.

## Supported Targets & Setup Overview

**No copyrighted system ROMs are bundled** in `czx` or its core binaries. Targets with open-source or clean-room fallback BIOSes boot standalone immediately; others require user-supplied ROM dumps under `~/czx/roms/`. Select any target row to open its full setup guide:

| Target (Detailed Guide) | Core ID | ROMs Required? (Case Rule) | ROM Directory (`~/czx/roms/...`) | Media Directory & Formats (`~/czx/media/...`) |
| :--- | :--- | :--- | :--- | :--- |
| [Apple II](docs/apple2.md) | `applewin` | **Yes** *(case-insensitive)* | `apple2/` | `apple2/` — `.woz` `.dsk` `.do` `.po` `.nib` `.2mg` `.hdv` `.zip` |
| [Commodore 64](docs/c64.md) | `vice_x64sc`, `vice_x64` | **Yes** *(case-sensitive lowercase)* | `vice/C64/`, `vice/DRIVES/` | `c64/` — `.d64` `.g64` `.prg` `.crt` `.tap` `.t64` `.zip` |
| [NEC PC-8801](docs/pc88.md) | `quasi88` | Optional *(pseudo BIOS fallback; UPPER/lower accepted)* | `quasi88/` | `pc88/` — `.d88` `.t88` `.cmt` `.m3u` `.zip` |
| [NEC PC-9801](docs/pc98.md) | `np2kai` | Optional *(HLE BIOS fallback; lowercase)* | `np2kai/` | `pc98/` — `.fdi` `.hdm` `.d88` `.nhd` `.hdi` `.cmd` `.zip` |
| [MSX / MSX2 / MSX2+](docs/msx.md) | `bluemsx` | Cartridges: **No** *(bundled C-BIOS)*<br>BASIC / `.dsk`: **Yes** *(case-insensitive in `Shared Roms/`)* | `bluemsx/Machines/Shared Roms/` | `msx/` — `.rom` `.mx1` `.mx2` `.dsk` `.cas` `.m3u` `.zip` |
| [SHARP PC-G850 / G815 / E200](docs/g800.md) | `g800` *(built-in)* | Optional *(compat BASIC/IOCS fallback; case-insensitive)* | `g800/` *(or `g850/`, `g815/`, `e200/`)* | `g800/` — `.ihx` `.hex` `.bas` `.txt` |
| [SHARP MZ-700](docs/mz700.md) | `emuz700` | **Yes** *(case-insensitive + aliases)* | `mz700/` | `mz700/` — `.mzt` `.mzf` `.m12` `.wav` `.tap` `.q20` `.d88` `.2d` |
| [SHARP X68000](docs/x68000.md) *(Experimental)* | `msx68k` | **Yes** *(case-insensitive)* | `x68000/` | `x68000/` — `.xdf` `.dim` `.2hd` `.2dd` `.hds` `.hdf` `.img` `.m3u` |

`czx` also includes a built-in `test_pattern` core (requires no ROMs or media) with interactive `280×192`, `560×192`, `320×200`, `384×272`, and `640×400` calibration patterns.

If any required ROM is missing, `czx` displays an on-screen **ROM FILES REQUIRED** prompt listing the target directory and missing files so you can switch cores (`Alt + 8`) or supply the ROMs without crashing.

## Display Modes (Desktop Native & Cardputer Zero LCD)

On desktop Linux (X11/SDL2), `czx` defaults to an integer-scaled **Desktop** mode (`1x` / `2x` / `3x`, default `2x`) showing each machine at full native resolution and automatically resizing the window on mode or aspect changes. On Cardputer Zero (or when `--lcd` / `--mode ...` is passed), `czx` uses its `320×170` LCD modes:

1. **1:1 Pan (`Alt + 4`)** — Unscaled `320×170` pixel-accurate LCD view with 4-way viewport scrolling (`Alt + F / Z / X / C` or `Alt + Arrow keys`).
2. **Axis Fit (`Alt + 5`)** — Separable area-resampling downscale **only along overflowing axes**; non-overflowing axes remain crisp `1:1`.
3. **Aspect Fit (`Alt + 6`)** — Aspect-preserving downscale to `320×170` (PAR-aware or `4:3` CRT selectable in menu, never upscales).
4. **Desktop (`1x`/`2x`/`3x`)** *(PC only, selectable via `Alt + 4/5/6`, `Alt + 8 → Display`, or `--scale <1|2|3>`)* — Integer-scaled auto-resizing native-resolution window with ARGB HUD/menu overlay.

## Cardputer Zero Controls

Holding `Alt` reveals the 5-slot icon bar aligned above physical keys `4 5 6 7 8`:

| Keys | Function |
| :--- | :--- |
| `Alt + 4` | Switch to **1:1 Pan** mode |
| `Alt + 5` | Switch to **Axis Fit** (overflow-only downscale) mode |
| `Alt + 6` | Switch to **Aspect Fit** mode |
| `Alt + 7` | Cycle **Keyboard** → **Joy P1** → **Joy P2** → **Mouse (`FZXC`)** (`F/Z/X/C` + `Space`/`Enter`) |
| `Alt + 8` | Open / Close **System Menu** (Core, Load Media / Drive 1 & 2, Eject / Tape Play, Display, Aspect, Borders, Margin, Input, Reset, Quit) |
| `Alt + F/Z/X/C` | Scroll viewport in **1:1 Pan** mode (hold `Shift` for 2× speed) |

- **Keyboard pass-through**: `SYM` and `Fn` keys pass through intact to the emulated machine (TCA8418 `SYM` scancodes map cleanly to shifted/unshifted US keys).
- **Media & multi-disk `.d88` support**: Insert, eject, or hot-swap disks anytime via `Alt + 8` (`F/X` move, `Z` up dir / eject drive, `C`/`Enter` open/load, `ESC` back). Apple II, PC-8801, and PC-9801 expose separate **Drive 1 / Drive 2** (`FDD 1 / FDD 2`) slots, and multi-disk `.d88` archives offer sub-disk selection plus `[AUTO] D1:#1 + D2:#2`.

## Directory Layout (`$CZX_DATA_DIR`, default `~/czx`)

`czx` (and `install-on-device.sh`) initializes the following directories on startup:

- `~/czx/media/<target>/` — Disk, tape, cartridge, and BASIC/HEX files per machine
- `~/czx/roms/<target>/` — User-supplied system ROMs (plus bundled open-source blueMSX C-BIOS & `Databases/`)
- `~/czx/saves/` — Persistent RAM, `vicerc`, dual-drive playlists, and MZ-700 `CMOS.BIN`
- `~/czx/config/<core_id>.cfg` — Optional per-core libretro `key = value` overrides (see individual guides in [`docs/`](docs/))

## Build & Run on Linux PC

Clone with submodules (`git clone --recurse-submodules`, or initialize them non-recursively via `./scripts/prepare-cores-src.sh`):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./scripts/build-cores-host.sh
```

Run directly from the command line — `czx` automatically selects the matching core from the media extension (or pass `--core <id>` explicitly):

```bash
./build/czx /path/to/game.mzt                          # Auto-selects emuz700 (Desktop 2x window)
./build/czx /path/to/HUMAN302.XDF                      # Auto-selects msx68k (auto-resizes window)
./build/czx --scale 1 /path/to/HUMAN302.XDF            # 1x integer-scaled desktop window (1, 2, or 3)
./build/czx /path/to/disk1.dsk /path/to/disk2.dsk      # Dual-drive media via positional args
./build/czx --core quasi88 --media disk1.d88 --media2 disk2.d88
./build/czx --lcd /path/to/game.d64                    # Preview 320x170 Cardputer Zero LCD simulator
```

On desktop Linux, `czx` opens at `2x` integer scale (configurable via `--scale <1|2|3>`, `Alt + 4/5/6`, or `Display` in `Alt + 8`), automatically resizing the window whenever the guest screen resolution or `Aspect` setting changes.

## Cross-Build & Deploy to Cardputer Zero (Recommended)

Cross-compile `czx` and all ARM64 cores via Docker (`debian:bookworm-slim`, zero OOM risk on the 512MB device):

```bash
./scripts/build-arm64.sh
rsync -av dist-arm64/ <user>@<cardputer-zero-host>:~/czx-dist/
```

Then on Cardputer Zero:

```bash
~/czx-dist/install-on-device.sh
```
