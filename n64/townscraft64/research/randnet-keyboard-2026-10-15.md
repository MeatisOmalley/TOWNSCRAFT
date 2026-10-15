# Randnet keyboard in ares (2026-10-15)

The official controls (Q5) are the N64 Mouse plus the Randnet keyboard
(RND-001). ares v148 has no keyboard (ares issue #1482), so Townscraft64
now has a pinned ares fork with one, and a test ROM that reads it with raw
Joybus commands. Stock ares v148 stays the default emulator until the fork
has been used for real work.

## Protocol

| Item | Value | Sources |
| --- | --- | --- |
| Info reply (`0x00`, `0xFF`) | `00 02 00`: identifier `0x0002`, third byte `0x00` | CP (captured), usb64, MiSTer; wiki and libdragon give the identifier only |
| Read keys | `0x13`, 2 bytes sent, 7 received | CP, wiki, libdragon (`JOYBUS_COMMAND_ID_N64_RANDNET_KEYBOARD_READ`) |
| Byte 2 of `0x13` (LEDs, 1 = on) | bit 0 Num Lock, bit 1 Caps Lock, bit 2 Power; bits 3–7 unknown | CP, usb64, MiSTer |
| Reply bytes 0–5 | three keys, each a 2-byte matrix position, high byte first; `0000` = empty | CP, raphnet, usb64, MiSTer, wiki |
| Reply byte 6 (status) | bit 0 Home held; bit 4 set when a fourth key is held, and then all three key slots are empty; other bits unknown | CP (bit 4 observed), raphnet, usb64, MiSTer |
| `0x01` (controller read) | no reply | CP command table; usb64; MiSTer differs (answers it) |
| Response time | 3.7–4.2 µs from command to reply, all commands | CP (logic analyser) |
| Randnet disk's polling rate | about 30 Hz | CP |

LEDs are pure outputs: the keyboard does not toggle them. The Randnet
software turns Caps Lock on only with Shift+Caps Lock, and the LEDs keep
their last state when the keyboard is not polled (CP).

Matrix positions (high byte, low byte) by Console Protocols key name, rows
top to bottom. The N64brew wiki lists the same positions under X11-style
names (`Muhenkan`, `Henkan`, `Kana_Lock`, `Prior`, `Next`, `Menu`, …); usb64
and MiSTer use the same values.

| Row | Keys |
| --- | --- |
| 1 | Esc 0A08, F1 0B01, F2 0A01, F3 0B08, F4 0A07, F5 0B07, F6 0A02, F7 0B02, F8 0A03, F9 0B03, F10 0A04, F11 0203, F12 0B06, Num Lock 0A05, key below Num Lock LED 0B05, below Caps Lock LED 0208, below Power LED 0207 |
| 2 | key above Tab (半角/全角) 0D05, 1 0C05, 2 0505, 3 0605, 4 0705, 5 0805, 6 0905, 7 0906, 8 0806, 9 0706, 0 0606, - 0506, ^ 0C06, Backspace 0D06 |
| 3 | Tab 0D01, Q 0C01, W 0501, E 0601, R 0701, T 0801, Y 0901, U 0904, I 0804, O 0704, P 0604, ' 0504, { 0C04, } 0406 |
| 4 | Caps Lock 0F05, A 0D07, S 0C07, D 0507, F 0607, G 0707, H 0807, J 0907, K 0903, L 0803, + 0703, * 0603, Enter 0D04 |
| 5 | Left Shift 0E01, Z 0D08, X 0C08, C 0508, V 0608, B 0708, N 0808, M 0908, < 0902, > 0802, ? 0702, long dash 1004, Up 0204, Right Shift 0E06 |
| 6 | Ctrl 1107, Opt 0F07, \| 1105, Alt 1008, 英数 1002, Space 0602, かな 0E02, Japanese character 1006, Home (status bit 0), End 0206, Left 0205, Down 0305, Right 0405 |

### Sources

- **CP**: fraser125, "Randnet Keyboard", Console Protocols,
  <https://sites.google.com/site/consoleprotocols/home/nintendo-joy-bus-documentation/n64-specific/randnet-keyboard>
  (read 2026-10-15). Bus captures of a real RND-001 in port 3 of an NTSC
  console with a Japanese 64DD, the Randnet disk and the modem cartridge.
  The command, LED, status and key tables are embedded HTML boxes on the
  page; they were read from the page source. The only primary source.
- **N64brew wiki**, "Joybus Protocol" (identifier `0x0002`; `0x13` "Read
  Keypress", 2/7 bytes; no byte layout) and "Keyboard" (revision 5638,
  2025-08-18: the matrix table and scan-code compression macros; no
  protocol description, no references). Note: when read on 2026-10-15 the
  "requires verification" marks on the Joybus page were on GameCube
  entries, not on the Randnet rows, but neither page gives a source for
  the Randnet data.
- **libdragon** `39d0d609`: `include/joybus.h`
  (`JOYBUS_IDENTIFIER_N64_RANDNET_KEYBOARD 0x0002`),
  `src/joybus/joybus_commands.h` (command `0x13`). No driver, no reply
  struct.
- **raphnet gcn64tools**, `src/pollraw.c` `pollraw_randnet_keyboard`: a
  PC tool that polls a real keyboard through raphnet's adapter with
  `13 00` and decodes the reply as above; credits fraser125.
- **usb64** (Ryzee119, commit `02b63b48`, 2024-09-14), `src/n64/n64_controller.[ch]`
  and `src/input.[ch]`: a USB-to-N64 adapter that emulates the keyboard.
  Same layout; keeps a key in its slot until released, does not use the
  error bit.
- **MiSTer N64 core** (commit `57253817`, 2026-09-28),
  `rtl/keyboard_scanner.vhd` and `rtl/Gamepad.vhd`: FPGA emulation from
  PS/2. Same layout and LED bits; keys in press order, gaps closed on
  release; four or more keys give the error bit with empty slots.
- **ares issue #1482** (open since 2024-05-04): the request; links CP.

A GitHub code search for `RANDNET` (2026-10-15) turned up only usb64,
gcn64tools, the MiSTer core and entii-for-workcubes (not read). No hardware
capture other than CP's was found. Other emulators were not checked.

### Unverified details

Nothing here has been checked on a real keyboard by us. Beyond that, the
sources disagree or are silent on:

1. **Slot order.** CP does not say which slot a key lands in. usb64 keeps
   each key in the first free slot until it is released; MiSTer keeps
   press order and closes gaps. The fork follows MiSTer. Real hardware may
   report in matrix scan order instead. *Driver rule: treat the three slots
   as an unordered set.*
2. **Error state.** CP saw bit 4 with no keys when four keys were held.
   Whether three keys plus Home also trigger it, whether ghosting
   combinations of three keys do, and whether the error clears as soon as
   one key is released are unknown. The fork: Home does not count, any
   four matrix keys give the error, three again clear it. *Driver rule:
   keep the previous key state through an error frame, otherwise held keys
   look released and pressed again.*
3. **Home** is only a status bit; whether it occupies a slot internally
   (usb64 effectively does) is unknown.
4. **Status bits 1–3 and 5–7**, and LED bits 3–7: meaning unknown, the fork
   sends 0.
5. **Reset (`0xFF`)**: whether it clears the LEDs or a stuck error is
   unknown; the fork keeps the LEDs.
6. **Short or odd requests**: `0x13` without the LED byte, or with a reply
   length other than 7, is undocumented. The fork answers only when the LED
   byte is present and flags an overrun for longer reads, like ares's
   gamepad.
7. **`0x01`**: CP lists no reply; MiSTer answers with controller data. The
   fork does not answer (the test ROM sees `0x84`: no device).
8. **Third info byte**: always `00` in CP's capture; whether it ever
   changes (e.g. on error) is unknown.
9. **Key legends.** Names come from CP's table (its "top-left character"
   labels such as `'`, `{`, `+`, long dash, `|`); the physical legends,
   and so the exact JIS correspondence of the punctuation keys, were not
   checked against a keyboard or the manual (CP links the manual PDF).
   The wiki's `Home = [0x10, 0x10]` is a placeholder, not a matrix code.
10. **Timing.** ares's PIF model charges a flat cost per connected device,
    so the keyboard's 3.7–4.2 µs response is not modelled.
11. **Auto-repeat** does not exist at the protocol level (the reply is a
    level, not events); repeat is the game's job.

## The ares fork

| | |
| --- | --- |
| Base | ares v148, commit `0aafd85789215e84e1e43415c07d4c88461b7899` (`ares-emulator/ares`) |
| Branch | `townscraft64-randnet-keyboard`, local clone at `C:\Users\meat\Documents\craft\n64\ares-randnet` (remote `upstream`, push disabled; **not published**) |
| Pinned commit | `a69c076b5b7c74cf03bad23018f0cb29ec534cb1` (two commits on v148) |
| Patch | [`patches/ares-v148-randnet-keyboard.patch`](../patches/ares-v148-randnet-keyboard.patch), `git format-patch` of both commits, SHA-256 `75648217…ff2aea5` |
| Build | MSYS2 CLANG64, clang 23.1.3, cmake 4.4.4, ninja, preset `windows-ci-mingw-native` (RelWithDebInfo) — the recipe of ares's own windows-x64 release |
| `ares.exe` | SHA-256 `a6f256df910dc368dbdf47581362e1c4e60b8a75c1b6b2aaa908b9dfb7008615`, 20,504,064 bytes |

`SDL3.dll` and `librashader.dll` come unchanged from ares-deps windows-x64
2026-04-23, which ares's `deps.json` pins by archive hash. SHA-256:
`SDL3.dll` `29f53e6922316b233e4e3d0b751ed3c78f4bdee907e9890060d65abb0513ddf7`,
`librashader.dll` `6ee19f548e5ee0598267c6b125992c19ed4fd4232e5df98d864555938dc00a99`.

The two commits:

1. `2284c77f` ymfm: `#include <cstdlib>` in `ymfm_opq.cpp`. Build-only:
   with today's MSYS2 clang 23.1.3, `std::abs` is no longer declared
   transitively there.
2. `a69c076b` the keyboard:
   - `ares/n64/controller/randnet-keyboard/`: a "Randnet Keyboard"
     peripheral offered on all four controller ports (`port.cpp`). It
     answers info with `00 02 00` and `0x13` as in the table above; keys
     are 85 button inputs named after CP's table. LED changes appear in the
     status bar ("Controller Port 2 keyboard LEDs: Power on, …").
   - `desktop-ui/emulator/nintendo-64.cpp`: host keys map by position
     while keyboard capture is on (ares's existing capture, used by its
     Famicom, MSX, SC-3000 and ZX Spectrum keyboards). A JIS keyboard maps
     one to one. A US keyboard gets the same physical keys plus fallbacks:

     | Randnet key | JIS host key | US fallback |
     | --- | --- | --- |
     | long dash (1004) | ろ (Ro) | Right Ctrl |
     | \| (1105) | ¥ (Yen) | Delete |
     | 英数 (1002) | 無変換 | Insert |
     | かな (0E02) | 変換 | Right Alt |
     | Japanese character (1006) | カタカナ/ひらがな | Right Win |
     | Num Lock, keys below the three LEDs | — | Print Screen, Scroll Lock, Page Up, Page Down |

     Others by position: `` ` `` → 半角/全角, `=` → `^`, `[` → `'`,
     `]` → `{`, `\` → `}`, `;` → `+`, `'` → `*`, `,` `.` `/` → `<` `>` `?`,
     Left Win → Opt, keypad Enter → Enter. Pause and the host's Num Lock
     cannot be read by ares's raw-input driver.
   - `ruby/input/keyboard/rawinput.cpp`: Katakana (`0x70`), Ro (`0x73`),
     Henkan (`0x79`), Muhenkan (`0x7B`) and Yen (`0x7D`) appended to the
     Windows keyboard (appended so saved bindings keep their indices).
   - Settings `Nintendo64/ControllerPort1`…`4` (default `Gamepad`; also
     `Mouse`, `Randnet Keyboard`) choose what is connected at load. A
     keyboard connected this way switches keyboard capture on. Chosen from
     the *Nintendo 64 → Controller Port N* menu instead, capture needs
     ares's *Toggle Keyboard Capture* hotkey (unbound by default).

Rebuild: check out the branch (or apply the patch to v148 and commit),
install `mingw-w64-clang-x86_64-{clang,cmake,ninja}` in MSYS2, then
`python tools/build_randnet_ares.py [--source <checkout>]`. It refuses a
checkout that is not at the pinned commit or has changes, copies the run
directory without PDBs to `build/deps/ares-randnet`, and prints the
`ares.exe` hash. A rebuild by another compiler will not reproduce the hash;
re-pin `RANDNET_ARES_EXE_SHA256` deliberately. `python
tools/fetch_dependencies.py --randnet-ares` checks the pin and records it
in `build/deps/acquired.json`.

Publishing: decided 2026-10-15 to keep the fork local for now (no GitHub
repository). If that changes, a `MeatisOmalley/ares` fork would mirror
DOScraft's `MeatisOmalley/86Box`, so the fork could be downloaded instead
of built.

## Test ROM

`src/kbtest.c` → `kbtest.z64` (`python tools/build.py kbtest.z64`;
SHA-256 of the tested build
`5b96a6e27d948c45a1ceb0449690785ef1b7fadf272956457d438dc3981a6c54`,
180,224 bytes). It uses `joybus_exec` directly (no joypad subsystem): `0x01`
once per port at start (reply flags shown as `01:xx`), then every frame
`0x00` on all ports and, on a port with identifier `0002`, `0x13` with the
LED byte. It shows the raw reply, decoded key names, Home and the error
bit, and the last eight key presses; Caps Lock and Num Lock toggle their
LEDs; the Power LED is always on.

```
python tools/build.py kbtest.z64
powershell -File tools/run_ares.ps1 -Rom kbtest.z64 -Randnet -Keyboard 2 -Mouse 1
```

## Verification (forked ares, pinned build)

Keys were injected as scancodes with `SendInput` into the ares window
(only while it was the foreground window) and the window captured from the
same process. Spawning a process between key events moved the focus and
lost events; that is a test-harness effect, not a fork bug.

| Check | Result |
| --- | --- |
| Port menu | *Nintendo 64 → Controller Port 1–4* each list Nothing, Gamepad, Mouse, Randnet Keyboard (read from the native menu); with `-Keyboard 2 -Mouse 1`, port 1 shows Mouse and port 2 Randnet Keyboard checked |
| Detection | keyboard port shows `id 0002 00`; `0x01` to it gives `84` (no device); mouse port `id 0200 02`; gamepads `0500` |
| Single keys | every mapped host key tried gave the table's code: letters, digits row, Enter, Tab, End, Up, Left Shift, Space, `` ` ``, `[`, `\`, `;`, `'`, Page Up/Down, Insert, Delete, and the JIS keys Yen, Ro, Henkan, Muhenkan, Katakana |
| Three keys | Left Shift, Z, Space → `0e01 0d08 0602 00` ([screenshot](randnet-2026-10-15/1-three-keys.png)) |
| Four keys | `0000 0000 0000 10` ([screenshot](randnet-2026-10-15/2-four-keys.png)) |
| Home | status `01`, no slot used |
| LEDs | Caps Lock press → ROM sends LED byte `06` → status bar "Controller Port 3 keyboard LEDs: Power on, Caps Lock on, Num Lock off" ([screenshot](randnet-2026-10-15/3-caps-led.png)); second press turns it off |
| Ports | keyboard tested on port 2 (mouse on 1) and port 3 (no mouse) |
| Stock default | `run_ares.ps1` without `-Randnet` still starts stock v148 |

Not tested: Escape, F-keys and Alt/Win (they can trigger ares hotkeys or
Windows menus during injection), the menu-driven connection path with the
capture hotkey, save states with the keyboard connected, a physical JIS
keyboard, and the mouse inputs in the fork.
