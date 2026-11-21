# czx — Retro Computer Multi-Emulator Platform for Cardputer Zero

`czx` is a lightweight C++20 multi-emulator platform built for the **M5Stack Cardputer Zero** (`320×170` RGB565 SPI LCD, TCA8418 I2C keyboard, Raspberry Pi CM0 ARM64 Debian), with an X11/SDL2 desktop simulator for PC testing.

## Supported Cores

**No copyrighted system ROMs are bundled** in `czx` or in the core `.so` binaries built by this repository. Place your own legal ROM dumps in `~/czx/roms/` before booting emulated machines (`test_pattern` runs standalone without external ROMs).

- **Apple II (`applewin_libretro.so`)** — High-accuracy `audetto/AppleWin` built strictly for external ROM loading (Apple IIe Enhanced, DHGR/80-col, WOZ/NIB/DSK/PO/2MG, Mockingboard, Disk II control).
- **Commodore 64 (`vice_x64sc_libretro.so` / `vice_x64_libretro.so`)** — Cycle-exact `VICE x64sc` (MOS 6569/6581, 1541-II true drive emulation, PRG/D64/G64/TAP/T64/CRT) plus lighter `VICE x64`, built with `-USE_EMBEDDED` for external ROM loading.
- **PC-8801 (`quasi88_libretro.so`)** — `QUASI88` (PC-8801mkIISR, N88-BASIC V2/V1H/V1S/N-BASIC, OPNA/OPN, D88/T88/CMT/M3U) with built-in clean-room pseudo BIOS fallback and external NEC ROM support.
- **PC-9801 (`np2kai_libretro.so`)** — `Neko Project II kai` (PC-9801VX, 2.4576 MHz ×4, PC9801-86 FM sound, FDI/HDM/D88/NHD/HDI) with built-in clean-room HLE BIOS fallback and external NEC ROM support.
- **MSX / MSX2 / MSX2+ (`bluemsx_libretro.so`)** — `blueMSX` (MSX/MSX2/MSX2+, V9938/V9958, PSG/YM2413/SCC, ROM/MX1/MX2/DSK/CAS/M3U) with open-source C-BIOS (2-Clause BSD) for cartridge boot and external MSX BIOS/Disk ROM support.
- **SHARP PC-G850 / G815 / E200 (`g800`, built-in)** — `g800` v0.11.4 (2-Clause BSD, built-in compatible BASIC/IOCS + external SHARP ROM support via `~/czx/roms/g800/`, crisp `2×` dot-matrix LCD with status indicators, IHX/HEX/BAS/TXT loading).
- **SHARP MZ-700 (`emuz700_libretro.so`)** — Takeda Toshiya's CSCP `EmuZ-700` (GPLv2) built strictly for external ROM loading (`~/czx/roms/mz700/`), native `320×200` RGB565 output, 1-bit sound + data recorder, MZT/MZF/M12/WAV/CAS/TAP/T77/Q20/D88/2D media, and JOY-700/AM7J/MZ-1X03 joystick support.
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
│   ├── c64/         # .prg / .d64 / .g64 / .tap / .t64 / .crt / .zip
│   ├── pc88/        # .d88 / .t88 / .cmt / .m3u / .zip
│   ├── pc98/        # .fdi / .hdm / .d88 / .nhd / .hdi / .zip
│   ├── msx/         # .rom / .mx1 / .mx2 / .dsk / .cas / .m3u / .zip
│   ├── g800/        # .ihx / .hex / .bas / .txt
│   └── mz700/       # .mzt / .mzf / .m12 / .wav / .cas / .tap / .t77 / .q20 / .d88 / .2d
├── roms/
│   ├── apple2/      # Required: Apple2e_Enhanced.rom, Apple2e_Enhanced_Video.rom, DISK2.rom
│   ├── vice/
│   │   ├── C64/     # Required: kernal-901227-03.bin, basic-901226-01.bin, chargen-901225-01.bin
│   │   └── DRIVES/  # Required: dos1541ii-251968-03.bin (+ optional 1541/1571/1581/JiffyDOS ROMs)
│   ├── quasi88/     # Optional: N88.ROM, N88_0..3.ROM, N88N.ROM, DISK.ROM, FONT.ROM, KANJI1.ROM
│   ├── np2kai/      # Optional: bios.rom, itf.rom, sound.rom, font.rom / font.bmp
│   ├── bluemsx/     # Open-source C-BIOS & Databases; real MSX BIOS/Disk ROMs in Machines/Shared Roms/
│   ├── g800/        # Optional: base.txt/base.bin + rom00.txt/rom00.bin.. (or g850/, g815/, e200/)
│   └── mz700/       # Required: IPL.ROM (or 1Z-009A/B, NEWMON7.ROM) & FONT.ROM (or MZ700FON.JP/DAT)
├── saves/           # Battery RAM / VICE vicerc / MZ-700 CMOS.BIN
└── config/          # Optional per-core libretro key=value overrides (<core_id>.cfg)
```

Required and optional user-supplied ROM files:

- **Apple II (`~/czx/roms/apple2/`, matched case-insensitively)**: `Apple2e_Enhanced.rom` (16 KiB), `Apple2e_Enhanced_Video.rom` (4 KiB), `DISK2.rom` (256 B). Additional model/slot ROMs (`Apple2_Plus.rom`, `Mockingboard-D.rom`, etc.) can be placed in the same directory when needed.
- **Commodore 64 (`~/czx/roms/vice/`)**: `C64/kernal-901227-03.bin` (8 KiB), `C64/basic-901226-01.bin` (8 KiB), `C64/chargen-901225-01.bin` (4 KiB), and `DRIVES/dos1541ii-251968-03.bin` (16 KiB).
- **PC-8801 (`~/czx/roms/quasi88/`)**: `quasi88-libretro` includes a clean-room pseudo BIOS fallback, but for full software compatibility place real PC-88 ROMs (`N88.ROM`, `N88_0.ROM`–`N88_3.ROM`, `N88N.ROM`, `DISK.ROM`, `FONT.ROM`, `KANJI1.ROM`) here.
- **PC-9801 (`~/czx/roms/np2kai/`)**: `NP2kai` includes a built-in HLE BIOS fallback, but for full compatibility (including Japanese text and authentic OPNA/BIOS calls) place `bios.rom`, `itf.rom`, `sound.rom`, and `font.rom` / `font.bmp` here. Avoid placing optional `d8000.rom` unless needed for HDDs, as it can stall floppy boots.
- **MSX / MSX2 / MSX2+ (`~/czx/roms/bluemsx/`)**: `czx` ships open-source C-BIOS (`MSX2+ - C-BIOS`, `MSX2 - C-BIOS`, `MSX - C-BIOS`) and XML `Databases/` for cartridge boot. For MSX-BASIC and `.dsk` disk software, place real MSX BIOS and Disk ROMs (e.g., `MSX2P.rom`, `MSX2PEXT.rom`, `MSX2PMUS.rom`, `PANASONICDISK.rom`, `XBASIC2.rom`, `MSXKANJI.rom`) in `~/czx/roms/bluemsx/Machines/Shared Roms/`.
- **SHARP PC-G850 / G815 / E200 (`~/czx/roms/g800/`)**: Works out of the box with `g800`'s built-in compatible BASIC, IOCS, and machine-language monitor. To run real SHARP ROMs, place `base.txt`/`base.bin` + `rom00.txt`/`rom00.bin`.. in `~/czx/roms/g800/` (or per-model subdirectories `g850/`, `g815/`, `e200/`). Pocket computer special keys: `F1`=BASIC, `F2`=TEXT, `F3`=2ndF, `F4`/`Home`=CLS, `F5`=CAPS, `F6`=KANA, `F7`=CONST, `F8`=ANS, `F9`/`Shift+ESC`=OFF, `F10`=MENU, `ESC`=ON/BRK.
- **SHARP MZ-700 (`~/czx/roms/mz700/`, matched case-insensitively)**: `IPL.ROM` (4 KiB, or `1Z-009A.ROM` / `1Z-009B.ROM` / `NEWMON7.ROM`) and `FONT.ROM` (4 KiB, or `MZ700FON.JP` / `MZ700FON.DAT`). Optional expansion ROMs (`MZ1R12.ROM`, `MZ1E14.ROM`, `MZ1E05.ROM`, `EMM.ROM`, `KANJI.ROM`/`MZ1R23.ROM`, `DICT.ROM`/`MZ1R24.ROM`) are also loaded from here when present. Special keys: `ESC`=BREAK, `BS`/`DEL`=DEL, `INS`=INST, `F1`–`F5`, `F6`/`PgUp`=KANA, `F7`/`PgDn`=EISUU, `F8`=GRAPH.

If any required ROM is missing, `czx` displays an on-screen **ROM FILES REQUIRED** prompt listing the target folder and missing files so you can switch cores (`Alt + 8`) or add the ROMs without crashing.

Insert or hot-swap disks anytime via `Alt + 8` → **Load Media** (`F/X` move, `Z` up dir, `C`/`Enter` open/load, `ESC` back).

## Build & Test on PC

Clone with submodules (`git clone --recurse-submodules`, or initialize them non-recursively via `./scripts/prepare-cores-src.sh` / `git submodule update --init --depth 1 third_party/AppleWin third_party/vice-libretro third_party/quasi88-libretro third_party/NP2kai third_party/blueMSX-libretro third_party/takeda-cscp-emulators`):

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
