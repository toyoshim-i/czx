# IBM PC / DOS (`dosbox_pure`)

`czx` runs IBM PC / MS-DOS emulation via [DOSBox Pure](https://codeberg.org/schelling/dosbox-pure) (`dosbox_pure_libretro.so`, GPL-2.0-or-later). No external system ROMs are required — it boots straight into DOS or launches `.zip` / `.exe` / `.com` / `.bat` / disk / CD images directly.

← [Back to README](../README.md)

## Quick Facts

| Item | Value |
| :--- | :--- |
| Core ID | `dosbox_pure` |
| Binary | `dosbox_pure_libretro.so` |
| ROM / System Directory | `~/czx/roms/dos/` *(optional SoundFonts `.sf2` only)* |
| External ROMs Required? | **No** (boots out of the box) |
| Media Directory | `~/czx/media/dos/` |
| Supported Media | Archives: `.zip`, `.dosz`<br>Executables / Scripts: `.exe`, `.com`, `.bat`, `.conf`<br>Disk / CD Images: `.img`, `.ima`, `.vhd`, `.iso`, `.chd`, `.cue`, `.m3u` |
| Native Output | `320×200`, `640×400`, `640×480`, `720×400` text, etc. |

## Media Setup & Usage

- Place `.zip` archives, `.exe`/`.com`/`.bat` programs, floppy `.img`/`.ima`, or CD `.iso`/`.cue`/`.chd` images in `~/czx/media/dos/`.
- **Booting a `.zip` directly**: DOSBox Pure mounts `.zip` files as a writable `C:` drive (saving any modifications cleanly into `~/czx/saves/` without modifying the original `.zip`) and displays a start menu to choose the executable inside.
- **Booting without media**: Launching `IBM PC / DOS` with no media selected opens the DOSBox Pure DOS prompt.
- **Controls**:
  - Keyboard and mouse input forward directly (on Cardputer Zero, cycle `Alt + 7` for `Keyboard` / `Joy P1` / `Joy P2` / `Mouse (FZXC)`).
  - USB gamepads map automatically via DOSBox Pure's auto-mapping (`dosbox_pure_auto_mapping = true`).

## Roland MT-32 / CM-32L MIDI (`munt` + MPU-401)

`dosbox_pure` routes its **MPU-401** (`intelligent` mode) MIDI output directly to `czx`'s built-in background-threaded `munt` (`libmt32emu`) synthesizer via `RETRO_ENVIRONMENT_GET_MIDI_INTERFACE`.

- Place `CM32L_CONTROL.ROM` + `CM32L_PCM.ROM` (preferred) or `MT32_CONTROL.ROM` + `MT32_PCM.ROM` in `~/czx/roms/mt32/`.
- Toggle **MT-32** in `Alt + 8` (`CM-32L` / `MT-32` / `Off` / `No ROM`, persisted in `~/czx/config/czx.cfg` as `mt32 = on|off`).
- When ROMs are present and `MT-32` is `On`, `czx` sets `dosbox_pure_midi = frontend` automatically so DOS games configured for Roland MT-32 / LAPC-I / MPU-401 play through `munt`. When `Off` or ROMs are absent, `dosbox_pure_midi = disabled` is set so games use Sound Blaster / AdLib cleanly.

## Configuration (`~/czx/config/dosbox_pure.cfg`)

Defaults seeded by `czx`:

```ini
dosbox_pure_midi = frontend            # Managed automatically by czx MT-32 toggle
dosbox_pure_auto_mapping = true        # Automatic gamepad-to-keyboard/joystick mapping
dosbox_pure_on_screen_keyboard = false # Cardputer Zero has a physical keyboard
dosbox_pure_bind_unused = false
```
