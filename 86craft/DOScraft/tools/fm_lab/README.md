# DOScraft FM Lab

A browser instrument lab for the Yamaha OPL2 FM chip that DOScraft's music
plays on. Design two-operator patches by ear, then paste them into `PATCHES`
in `tools/build_music_pack.py`.

The sound comes from [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3), a
cycle-accurate YMF262 emulator, compiled unmodified to WebAssembly and run in
an AudioWorklet. In OPL2 mode the YMF262 is register-compatible with the YM3812;
the chips differ mainly in the output DAC. The page asks the browser for a
49,716 Hz AudioContext (the chip's own rate) so the emulator output is not
resampled before the browser's own output stage; if the browser refuses, Nuked
OPL3's resampler converts to the device rate and the page says so.

Fallbacks, for pages whose security policy is stricter than a plain web server
(the published Artifact runs inside claude.ai's sandbox):

- If WebAssembly is refused, the worklet runs `opl3_asm.js`, the same module
  translated to JavaScript by Binaryen's wasm2js. Its output is bit-identical to
  the WebAssembly build (checked by hashing several seconds of rendered audio)
  and it renders about 17 times faster than real time.
- If no AudioWorklet module can load (from a blob: or a data: URL), the same
  engine runs on the main thread through a ScriptProcessorNode. That can stutter
  while the page is busy, so it is only a last resort.

The line under the oscilloscope names the engine in use. Add `#js` or `#main`
to the page URL to force either fallback for testing.

## Using it

Open `fm_lab.html` from a web server or the published Artifact. AudioWorklet
needs a secure origin, so opening the file straight from disk (`file://`) works
in some browsers and not others; for a local copy run:

    uv run --no-project python -m http.server 8765 --directory tools/fm_lab

and browse to <http://localhost:8765/fm_lab.html>. The page's "How to use"
section covers the keys. In short: letter keys play two rows of a piano,
`-`/`=` shift the octave, Space toggles sustain, Esc stops every note.

What matches the game:

- 9 voices, allocated like `build_music_pack.py` (free voice idle longest,
  else the oldest note is taken over).
- Pitch from the same F-number table and block formula as
  `src/platform/dos/opl_music.c`, with notes folded into MIDI 12-107.
- Velocity becomes carrier attenuation in 0.75 dB steps with the game's
  formula (channel volume 100, expression 127), added to the carrier's 40h.
- Register writes in the game's order on note-on (key off, carrier level,
  F-number, key on), spaced a couple of chip samples apart through Nuked's
  write buffer.

One known difference: the game's `note()` rewrites the carrier's 40h as the
level alone, dropping the KSL bits. The lab keeps them (the chip's behaviour
for the patch as written) and warns when a patch sets carrier KSL. None of the
current presets do.

The game patch line is 11 bytes: modulator 20h 40h 60h 80h E0h, carrier the
same, then C0h (feedback << 1 | connection). "Copy Python row" gives a line
ready for `PATCHES`; the paste field accepts either form back.

SBI files: 52 bytes, `SBI` 1Ah, a 32-byte name, then modulator/carrier pairs
of 20h, 40h, 60h, 80h, E0h, then C0h and 5 unused bytes. The published Artifact
can only hand out certain file types, so there the SBI arrives inside a zip;
served locally it downloads as a plain `.sbi`.

The "OPL3-only extras" drawer (waveforms 4-7, four-operator voices, stereo
pan, chip-wide deep tremolo/vibrato) never changes the game line, which always
shows what the game would play.

## Layout

| Path | What |
| --- | --- |
| `nuked/opl3.c`, `nuked/opl3.h`, `nuked/LICENSE` | Nuked OPL3 1.8, unmodified, from github.com/nukeykt/Nuked-OPL3 at commit `765ec962e473aeb767e4cba74ffdc8f588ffbfe8` |
| `src/opl_wasm.c` | WebAssembly entry points (reset, buffered register write, render) |
| `src/worklet.js` | AudioWorklet processor that runs the module |
| `src/lab.html`, `src/lab.js` | The page and its script |
| `build_fm_lab.py` | Builds `opl3.wasm` and assembles `fm_lab.html` |
| `src/engine.js` | Chip engine shared by the worklet and the main-thread fallback |
| `opl3.wasm` | Built module (about 10 KB, no imports) |
| `opl3_asm.js` | The same module as JavaScript, from Binaryen wasm2js |
| `fm_lab.html` | Built single-file page with the module inlined |

## Rebuilding

The WebAssembly is built with zig's bundled clang, which `uv` fetches from
PyPI (the `ziglang` package), so no system compiler or Emscripten is needed:

    uv run --no-project --with ziglang python tools/fm_lab/build_fm_lab.py

That compiles `src/opl_wasm.c` and `nuked/opl3.c` with
`--target=wasm32-wasi -O2 -mexec-model=reactor` into `opl3.wasm`, translates
it with `npx -p binaryen@123.0.0 wasm2js` into `opl3_asm.js` (needs Node.js),
and inlines both, the worklet and the script into `fm_lab.html`. After editing
only the page, skip those two steps:

    uv run --no-project python tools/fm_lab/build_fm_lab.py --skip-wasm

`--artifact PATH` also writes the page body alone (no `<html>`/`<head>`
wrapper), the form used to publish it as a claude.ai Artifact.

To update Nuked OPL3, replace the three files in `nuked/` with a new upstream
release, note the commit above, and rebuild.

## Licenses

Nuked OPL3 is copyright (C) 2013-2020 Nuke.YKT and is licensed under the GNU
Lesser General Public License, version 2.1 or later; see `nuked/LICENSE`. It is
used unmodified. Because `opl3.wasm` and `fm_lab.html` contain a compiled copy
of it, they are distributed together with its complete source and the build
script above, which lets anyone rebuild them from modified Nuked OPL3 source,
as the LGPL requires. The page's About section names the library, its author
and its license.

`opl3_asm.js` is a machine translation of `opl3.wasm` and carries the same
LGPL terms; Binaryen (Apache License 2.0) is only the tool that produces it.

`src/opl_wasm.c` is a thin wrapper that compiles into the same module and is
licensed under the LGPL 2.1 or later like Nuked OPL3. The rest of this folder
(`src/engine.js`, `src/worklet.js`, `src/lab.html`, `src/lab.js`, `build_fm_lab.py`) is part of
DOScraft.

The page loads Chakra Petch, Atkinson Hyperlegible and JetBrains Mono from
Google Fonts (SIL Open Font License); it falls back to system fonts offline.
