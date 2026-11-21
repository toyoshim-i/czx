# czx — Retro Computer Multi-Emulator Platform for Cardputer Zero

`czx` is a lightweight C++20 multi-emulator platform built for the **M5Stack Cardputer Zero** (`320×170` RGB565 SPI LCD, TCA8418 I2C keyboard, Raspberry Pi CM0 ARM64 Debian), with an X11/SDL2 desktop simulator for PC testing.

## Supported Cores

**No copyrighted system ROMs are bundled** in `czx` or in the core `.so` binaries built by this repository. Place your own legal ROM dumps in `~/czx/roms/` before booting emulated machines (`test_pattern` runs standalone without external ROMs).

- **Apple II (`applewin_libretro.so`)** — High-accuracy `audetto/AppleWin` built strictly for external ROM loading (Apple IIe Enhanced, DHGR/80-col, WOZ/NIB/DSK/PO/2MG, Mockingboard, Disk II control).
- **Commodore 64 (`vice_x64sc_libretro.so` / `vice_x64_libretro.so`)** — Cycle-exact `VICE x64sc` (MOS 6569/6581, 1541-II true drive emulation, PRG/D64/G64/TAP/T64/CRT) plus lighter `VICE x64`, built with `-USE_EMBEDDED` for external ROM loading.
- **Built-in Resolution Test Core (`test_pattern`)** — Interactive `280×192`, `560×192`, `320×200`, `384×272`, and `640×400` patterns.

## Display Scaling & Scroll Modes

Because virtually every retro computer exceeds Cardputer Zero's `320×170` LCD, `czx` provides instant switching among three display modes:

1. **1:1 Pan (`Alt + 4`)** — Unscaled pixel-accurate output with 4-way viewport scrolling (`Alt + F / Z / X / C` or `Alt + Arrow keys`).
2. **Axis Fit (`Alt + 5`)** — Separable area-resampling downscale **only along overflowing axes**; non-overflowing axes remain crisp `1:1`.
3. **Aspect Fit (`Alt + 6`)** — Aspect-preserving downscale (PAR-aware or `4:3` CRT selectable in menu, never upscales).

## Cardputer Zero Controls

Holding `Alt` reveals the 5-slot icon bar aligned above physical keys `4 5 6 7 8`:

| Keys | Function |
| :--- | :--- |
| `Alt + 4` | Switch to **1:1 Pan** mode |
| `Alt + 5` | Switch to **Axis Fit** (overflow-only downscale) mode |
| `Alt + 6` | Switch to **Aspect Fit** mode |
| `Alt + 7` | Cycle **Keyboard** → **Joystick P1** → **Joystick P2** (`F/Z/X/C` + `Space`/`Enter`) |
| `Alt + 8` | Open / Close **System Menu** (Core, Load Media, Eject, Display, Aspect, Borders, Margin Auto/Black, Reset, Quit) |
| `Alt + F/Z/X/C` | Scroll viewport in **1:1 Pan** mode (hold `Shift` for 2× speed) |

`SYM` and `Fn` keys pass through intact to the emulated retro computer (TCA8418 `SYM` scancodes map cleanly to shifted/unshifted US keys).

## Media & ROM Directory Layout

On startup, `czx` creates `$CZX_DATA_DIR` (default `~/czx`):

```text
~/czx/
├── media/
│   ├── apple2/      # .dsk / .do / .po / .nib / .woz / .2mg / .zip
│   └── c64/         # .prg / .d64 / .g64 / .tap / .t64 / .crt / .zip
├── roms/
│   ├── apple2/      # Required: Apple2e_Enhanced.rom, Apple2e_Enhanced_Video.rom, DISK2.rom
│   └── vice/
│       ├── C64/     # Required: kernal-901227-03.bin, basic-901226-01.bin, chargen-901225-01.bin
│       └── DRIVES/  # Required: dos1541ii-251968-03.bin (+ optional 1541/1571/1581/JiffyDOS ROMs)
├── saves/           # Battery RAM / VICE vicerc
└── config/          # Optional per-core libretro key=value overrides (<core_id>.cfg)
```

Required user-supplied ROM files:

- **Apple II (`~/czx/roms/apple2/`, matched case-insensitively)**: `Apple2e_Enhanced.rom` (16 KiB), `Apple2e_Enhanced_Video.rom` (4 KiB), `DISK2.rom` (256 B). Additional model/slot ROMs (`Apple2_Plus.rom`, `Mockingboard-D.rom`, etc.) can be placed in the same directory when needed.
- **Commodore 64 (`~/czx/roms/vice/`)**: `C64/kernal-901227-03.bin` (8 KiB), `C64/basic-901226-01.bin` (8 KiB), `C64/chargen-901225-01.bin` (4 KiB), and `DRIVES/dos1541ii-251968-03.bin` (16 KiB).

If any required ROM is missing, `czx` displays an on-screen **ROM FILES REQUIRED** prompt listing the target folder and missing files so you can switch cores (`Alt + 8`) or add the ROMs without crashing.

Insert or hot-swap disks anytime via `Alt + 8` → **Load Media** (`F/X` move, `Z` up dir, `C`/`Enter` open/load, `ESC` back).

## Build & Test on PC

Clone with submodules (`git clone --recurse-submodules`), or initialize them via `git submodule update --init third_party/AppleWin third_party/vice-libretro` (`./scripts/prepare-cores-src.sh` also initializes them automatically):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./scripts/build-cores-host.sh
./build/czx
```

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
