# Fujitsu FM-7 / FM77AV / FM77AV40 (`efm7`, `efm77av`, `efm77av40`)

`czx` runs Fujitsu FM-7, FM77AV, and FM77AV40 emulation via Takeda Toshiya's Common Source Code Project (`eFM-7`, `eFM77AV`, `eFM77AV40`, GPLv2, compiled standalone as `efm7_libretro.so`, `efm77av_libretro.so`, and `efm77av40_libretro.so`) strictly with external ROM loading.

← [Back to README](../README.md)

## Quick Facts

| Item | Value |
| :--- | :--- |
| Core IDs | `efm7` (FM-7), `efm77av` (FM77AV), `efm77av40` (FM77AV40) |
| Binaries | `efm7_libretro.so`, `efm77av_libretro.so`, `efm77av40_libretro.so` |
| ROM Directory | `~/czx/roms/fm7/` |
| ROM Case Sensitivity | **Case-insensitive** |
| External ROMs Required? | **Yes** (see ROM table below) |
| Media Directory | `~/czx/media/fm7/` |
| Supported Media | Floppy: `.d77`, `.77d`, `.d88`, `.2d`<br>Tape: `.t77`, `.wav`<br>Playlist: `.m3u` |
| Native Output | `640×200`, `320×200`, `640×400` (`RGB565`) |

## ROM Setup

Place your legal FM-7 / FM77AV / FM77AV40 ROM dumps in `~/czx/roms/fm7/` (matched case-insensitively):

### Required ROMs

| File | Required For | Purpose |
| :--- | :--- | :--- |
| `FBASIC30.ROM` *(or `FBASIC300.ROM`, `FBASIC301.ROM`, `FBASIC302.ROM`)* | All models | F-BASIC V3.0 ROM |
| `SUBSYS_C.ROM` | All models | Sub-system Type-C ROM (FM-7/FM77AV compatible) |
| `BOOT_BAS.ROM` *(or `BOOT_DOS.ROM`, `BOOT_TL11_11.ROM`)* | `efm7` (FM-7) | FM-7 boot ROM |
| `INITIATE.ROM` | `efm77av`, `efm77av40` | FM77AV / FM77AV40 initiator ROM |
| `SUBSYSCG.ROM` | `efm77av`, `efm77av40` | Sub-system character generator ROM |

### Optional ROMs

| File | Purpose |
| :--- | :--- |
| `SUBSYS_A.ROM`, `SUBSYS_B.ROM` | FM77AV Type-A / Type-B sub-system ROMs |
| `KANJI1.ROM` *(or `KANJI.ROM`)*, `KANJI2.ROM` | JIS Level 1 / Level 2 Kanji ROMs |
| `DICROM.ROM`, `EXTSUB.ROM` | FM77AV40 dictionary & extended sub-system ROMs |
| `BOOT_DOS.ROM`, `BOOT_MMR.ROM`, `BOOT_1MB.ROM` | Additional FM-7 boot ROMs |

- **Model Selection**: In the launcher, the **FM-7** target provides a **Model** selector (`FM-7` / `FM77AV` / `FM77AV40`). If `INITIATE.ROM` is absent and an FM-7 boot ROM (`BOOT_BAS.ROM` / `BOOT_TL11_11.ROM`) is present, `czx` automatically defaults to `FM-7` (`efm7`).
- If any required ROM for the chosen model is missing, `czx` displays a **ROM FILES REQUIRED** screen listing `~/czx/roms/fm7/` and the missing files.

## Media Setup & Usage

- Put floppy and tape images in `~/czx/media/fm7/`.
- **Dual Floppy (`FDD 0` / `FDD 1`)**: Open `Alt + 8` and select **FDD 0** or **FDD 1** to mount, hot-swap, or eject disks while running. Multi-disk `.d77` / `.d88` images support sub-disk selection and `[AUTO] D1:#1 + D2:#2`.
- **Tape (`.t77`, `.wav`)**: Mount via `Alt + 8` → **Load Media** (and toggle playback via **Tape Play**). **Tape Warp** (`Alt + 8 → Tape Warp: On` by default, persisted in `~/czx/config/czx.cfg` as `tape_warp = on|off`) accelerates tape loading when `efm7_tape_sound = disabled`.

## Configuration (`~/czx/config/efm7*.cfg`) & Save Data

Per-core options live in `~/czx/config/efm7.cfg`, `efm77av.cfg`, or `efm77av40.cfg`. Defaults seeded by `czx`:

```ini
efm7_boot_mode = auto          # auto | basic | dos
efm7_drive = FDD0              # FDD0 | FDD1 | CMT (active hot-swap slot)
efm7_sound_board = PSG+OPN     # efm7: PSG+OPN | PSG  (efm77av/av40: OPN | OPN+WHG)
efm7_tape_sound = disabled     # disabled | enabled
efm7_tape_turbo = enabled      # enabled | disabled
```

- **Save Data**: Writable state and temporary files are stored under `~/czx/saves/fm7/` so `~/czx/roms/fm7/` remains read-only.
