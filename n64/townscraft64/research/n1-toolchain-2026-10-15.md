# N1 toolchain bring-up (2026-10-15)

## Pinned dependencies

`tools/fetch_dependencies.py` downloads these into ignored `build/deps` and
checks them:

| Dependency | Pin | Notes |
| --- | --- | --- |
| MIPS GCC toolchain (Windows) | `gcc-toolchain-mips64-win64.zip`, SHA-256 `bd201c35…b4d1d0`, 103,193,777 bytes | GCC 16.2.0, binutils 2.45. libdragon's `toolchain-continuous-prerelease` asset (2026-09-15 upload). The tag is continuous: a future re-upload changes the hash and the script refuses it. |
| libdragon | `preview` branch, commit `39d0d609` (2026-09-14) | Built from source into the toolchain directory. `preview` has rdpq, the GL port and the 64DD/RTC code. |
| ares | v148 `ares-windows-x64.zip`, SHA-256 `fa97958e…037298` | Release of 2026-05-30. |

Host requirements: Python 3, Git, and MSYS2 with the UCRT64 environment
(gcc, make) at `C:\msys64` (or `MSYS2_ROOT`). libdragon's host tools
(`mksprite`, `audioconv64`, `n64tool`, …) are built with MSYS2's UCRT64 gcc
and run inside that environment.

## Build and run

```
python tools/fetch_dependencies.py
python tools/build.py              # builds libdragon on first use, then the ROM
powershell -File tools/run_ares.ps1               # 4 MB base console
powershell -File tools/run_ares.ps1 -ExpansionPak # 8 MB
```

ares runs with a private settings file in `build/ares-runtime`. ares
defaults the Expansion Pak to **on**; the launcher sets it explicitly,
defaulting to off for the 4 MB target.

### Paths with spaces

GNU make and `n64.mk` break on spaces. This worktree lives under
`Unreal Projects`, so `tools/build.py` builds through a junction in the temp
folder and overrides make's `CURDIR` (make resolves the junction back to the
real path otherwise). Through a junction the toolchain's `gcc-ar` cannot
find `liblto_plugin.dll`, so `N64_AR` becomes plain `ar --plugin <dll>`. The
main checkout (`C:\Users\meat\Documents\craft`) has no spaces and needs
none of this, but the workaround is harmless there.

## Hello-triangle ROM

`src/main.c`: an RDP-shaded spinning triangle, a guest-timed frame counter,
detected RAM size, and a per-port report (libdragon style, raw Joybus
identifier, stick/mouse deltas and their running sum, A/B buttons). The
running sum was added after the screenshots, because one-frame mouse deltas
are too brief to see. ROM 196,608 bytes; ELF 255,292 bytes of
text+data+bss, almost all libdragon. Hash of the tested build:
`072b8dc2905924e9366cfd84e0e264b6192177d37d6cf9ff67d391612e097ebb`.

Results in ares v148 (screenshots in `n1-2026-10-15/`):

| Config | RAM reported | Guest frame time | ares |
| --- | --- | --- | --- |
| Expansion Pak off | 4 MB | 16.71 ms | 60 VPS |
| Expansion Pak on | 8 MB | 16.75 ms | 60 VPS |

The frame time is vsync-locked (16.68 ms at NTSC 60 Hz). This shows the
toolchain and RDP path work; it is not a performance measurement.

All four ports report an N64 controller (identifier `0x0500`), ares's
default.

## Peripheral support

| Device | ares v148 | libdragon `39d0d609` | Result |
| --- | --- | --- | --- |
| N64 Mouse | Supported: controller port device "Mouse" (`ares/n64/controller/mouse`) | `JOYPAD_STYLE_MOUSE`; deltas in `stick_x/stick_y`, left/right buttons as A/B | **Works.** User-verified 2026-10-15 in ares v148: P2 reports Mouse, motion and buttons reach the ROM. |
| Randnet keyboard | **Not supported.** Ports offer only Gamepad, Mouse (and Aleck64). Open request: ares issue #1482. | Only the raw Joybus command ID (`0x13`) and identifier (`0x0002`) are defined; no driver. | Needs an **ares fork** with a keyboard device, and our own keyboard driver. Fork done 2026-10-15: [keyboard report](randnet-keyboard-2026-10-15.md). |
| 64DD | Emulated (`ares/n64/dd`, "Nintendo 64DD" system). Needs the 64DD IPL firmware, which is not downloaded here (it must come from the user's own hardware, like DOScraft's ROMs). | Detection, ASIC register access and the 64DD RTC only (`include/dd.h`); **no disk read/write driver**. | Emulator side available; we write the disk driver at N8. |

### Using the mouse in ares

One-time setup (saved in the launcher's private `build/ares-runtime/settings.bml`):

1. *Settings* → *Input*: system *Nintendo 64*, *Controller Port 2*, device
   *Mouse*. Right-click the mapping cells: X → *Mouse X-axis*, Y →
   *Mouse Y-axis*, Left/Right → *Mouse Left/Right Button*. (Pressing a key
   cannot map an axis.)
2. *Settings* → *Hotkeys* → *Toggle Mouse Capture*: assign a key. There is
   no default.

Every launch:

1. `powershell -File tools/run_ares.ps1`
2. *Nintendo 64* → *Controller Port 2* → *Mouse*. ares does not remember
   port devices between launches.
3. Click the game picture and press the capture key; the cursor disappears.
   **ares ignores mouse motion and buttons entirely until captured**
   (`desktop-ui/input/input.cpp` checks `ruby::input.acquired()`).
4. The ROM's `P2` line shows `Mouse 0200`; `sum` accumulates motion and
   `A`/`B` show the left/right buttons.

## Consequences for the plan

- Q5 (official keyboard + mouse) requires a pinned ares fork with a Randnet
  keyboard device, as DOScraft forked 86Box. Protocol details on the N64brew
  wiki are marked as needing verification, so the fork and our driver must
  be checked against each other and against any hardware captures we can
  find.
- N8 (Expansion Disk) needs our own 64DD disk driver; libdragon only covers
  detection and the RTC.
