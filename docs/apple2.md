# Apple II (`applewin`)

`czx` runs Apple IIe Enhanced emulation via `audetto/AppleWin` (`applewin_libretro.so`) built strictly without embedded copyrighted ROMs.

← [Back to README](../README.md)

## Quick Facts

| Item | Value |
| :--- | :--- |
| Core ID | `applewin` |
| Binary | `applewin_libretro.so` |
| ROM Directory | `~/czx/roms/apple2/` |
| ROM Case Sensitivity | Case-insensitive |
| External ROMs Required? | **Yes** (`Apple2e_Enhanced.rom`, `Apple2e_Enhanced_Video.rom`, `DISK2.rom`) |
| Media Directory | `~/czx/media/apple2/` |
| Supported Media | `.dsk`, `.do`, `.po`, `.nib`, `.woz`, `.2mg`, `.2img`, `.hdv`, `.m3u`, `.yaml`, `.zip`, `.bin`, `.gz`, `.iie`, `.apl` |
| Native Output | `560×192` (PAR `1:2`, de-doubled from AppleWin's `560×384`) |

## ROM Setup

Place your legal ROM dumps in `~/czx/roms/apple2/` (matched case-insensitively):

| File | Size | Status | Purpose |
| :--- | :--- | :--- | :--- |
| `Apple2e_Enhanced.rom` | 16 KiB | **Required** | Apple IIe Enhanced system ROM |
| `Apple2e_Enhanced_Video.rom` | 4 KiB | **Required** | Apple IIe Enhanced character generator |
| `DISK2.rom` | 256 B | **Required** | Disk II 16-sector P5 BOOT ROM |
| `Apple2e.rom`, `Apple2_Plus.rom`, `Apple2.rom`, `Apple2_JPlus.rom` | Varies | Optional | Alternative Apple II model ROMs |
| `Apple2_Video.rom`, `Apple2_JPlus_Video.rom`, `DISK2-13sector.rom` | Varies | Optional | Alternative video / 13-sector Disk II ROMs |
| `Mockingboard-D.rom`, `SSC.rom`, `MouseInterface.rom`, `Parallel.rom`, `ThunderClockPlus.rom` | Varies | Optional | Expansion card slot ROMs |
| `Hddrvr.bin`, `Hddrvr-v2.bin`, `HDC-SmartPort.bin` | 256 B | Optional | Hard disk controller firmware |

If any of the three required ROMs is missing, `czx` displays a **ROM FILES REQUIRED** screen without crashing so you can switch cores (`Alt + 8`) or add the files.

## Media Setup & Usage

- Put disk images in `~/czx/media/apple2/`.
- **Dual Disk II drives**: Open the System Menu (`Alt + 8`) and select **Drive 1** or **Drive 2** (`OK`/`Enter` to browse and insert, `Z` to eject).
- **Single-floppy hot-swap**: While already running a floppy image in Drive 1, inserting another floppy image into Drive 1 uses AppleWin's libretro disk control interface without rebooting.
- **Dual-drive boot**: When both Drive 1 and Drive 2 hold floppy images, `czx` generates `~/czx/saves/czx_applewin (MD).m3u` and boots with both disks attached. Hard disk images (`.hdv`) and `.m3u`/`.yaml`/`.zip` files load directly via Drive 1.

## Display & Controls

- **Recommended display mode**: **Axis Fit** (`Alt + 5`) scales the `560×192` hires framebuffer cleanly into `320×170`, while **1:1 Pan** (`Alt + 4`) shows pixel-exact hires text/graphics with `Alt + F / Z / X / C` scrolling.
- **Joystick (`Alt + 7`)**: Cycle to `Joy P1` or `Joy P2` (`F/Z/X/C` directions, `Space` = Button 0 / `FIRE1`, `Enter` = Button 1 / `FIRE2`).
- **Mouse (`Alt + 7`)**: Cycle to `Mouse (FZXC)` if using Mouse Interface software.

## Configuration (`~/czx/config/applewin.cfg`)

Optional key=value overrides read on core startup (`czx` seeds this default automatically):

```ini
applewin_floppy_multidrive = enabled
```
