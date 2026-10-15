# N2 portable core (2026-10-15)

> **Corrections (N2b, same day):** `vendor/doscraft` was re-pinned from
> `2ce6b443` to `c4ce3b7b` (6-bit block IDs, save version 6); the Towns snapshot
> is unchanged. And `render.c` did **not** compile in N2: `fmath.h` holds x86
> inline assembly, and the assembler's errors were missed by a case-sensitive
> search. Both are fixed and described in
> [the N2b report](n2b-doscraft-source-2026-10-15.md).

## 1. Pinned DOScraft snapshot

`tools/import_doscraft.py` copies files from **git objects** at pinned
commits (never a working tree) into `vendor/`, with a manifest listing each
file's path, git blob, SHA-256 and size. `--verify` re-checks every byte and
rejects unlisted files. `.gitattributes` marks `vendor/**` as `-text`, so
line endings are never normalized.

| Import | Commit | Files | Bytes | Contents |
| --- | --- | ---: | ---: | --- |
| `vendor/doscraft` | DOScraft `2ce6b443` (master, 2026-10-15) | 353 | 3,183,332 | `vendor/` (the Towns snapshot `d706a67d` with its own manifest), `src/`, `tools/`, `assets/`, nine design notes |
| `vendor/towns-tests` | Towns `d706a67d` | 25 | 108,173 | `tests/*.c`, `tests/*.inc` (not part of DOScraft's Towns snapshot) |

Not imported from DOScraft: `tests/` (22 MB of PC fixtures), `research/`,
`ISO/`, `profiles/`, `guest/`, `patches/`. Bring individual DOScraft tests
over with the feature they cover.

Finding: DOScraft's game is not a source tree. It is the Towns snapshot plus
fail-closed Python overlays (`tools/dos_*.py`) applied at build time, plus
DOS platform code. Many game features (chickens, 20-minute day, crafting
menu, menus, mob despawn, viewmodel, block textures) exist only as overlays.
See [FEATURE_CHECKLIST.md](../FEATURE_CHECKLIST.md).

## 2. Feature checklist

[FEATURE_CHECKLIST.md](../FEATURE_CHECKLIST.md) lists every DOScraft
feature with its source and a tag: core, extract, reimplement or drop. It
also records the flag set of the shipped DOScraft build.

## 3. Towns core for MIPS

`tools/stage_core.py` copies the Towns core from the snapshot (checked
against its manifest) into `build/core/src` and records every change in
`build/core/stage.json`. Three files change, by exact replacement:

| File | Change | Why |
| --- | --- | --- |
| `common.h` | `#include "hw.h"` → `n64_compat.h`; drop `NULL` and five libc prototypes | `hw.h` holds x86 port I/O; newlib supplies `NULL` and the string functions. |
| `hdd.h` | `#include "hw.h"` → `common.h` | Only the types are needed. |
| `libc.c` | Remove `memset`/`memcpy`/`memmove`/`memcmp`/`strlen` | x86 `rep` assembly; newlib's versions are used. `itoa_dec`, `rnd`, `hash3` stay. |

`n64_compat.h` defines the Towns integer types and asserts the ABI the core
assumes: 32-bit `int`, `long`, pointers and `size_t`, signed `char`.
libdragon's `-mabi=o64` matches all of them.

Not staged (replaced on the N64): `video.c`, `gfx.c`, `sys.c`, `sound.c`,
`save.c`, `hdd.c`, `bench.c`, `crt0.S`, `isr.S`, `trap.S`, `kernel.ld`.
`raster.c` is staged but not compiled: it is the x86 software rasterizer.

`python tools/build.py core` compiles 13 files (blocks, fmath, font, game,
inventory, libc, mobs, physics, player, render, tables, textures, ui, world)
with libdragon's flags plus `-std=gnu99 -Wall -Wextra -Wcast-align=strict`.
Warnings are not errors here, as in the Towns build, except implicit
declarations. Result: 135,064 bytes of text, 94,898 bytes of BSS, and five
warnings:

| Warning | Where | Assessment |
| --- | --- | --- |
| `-Wcast-align` ×3 | `column_hdd.inc:24-25`, `((u32 *)columnScratch)[4..6]` | Reads a little-endian HDD header through a byte buffer. Alignment and endianness both unsafe on MIPS, but this is the Towns HDD scratch format, which the N64 replaces (Q6). |
| `-Wsign-compare` | `column_cache.inc:156` | Benign. |
| unused variable | `world.c:855` | Benign. |

Static endian audit of the staged core: no unions, packed structs,
`#pragma pack`, or pointer reinterpretation outside `column_hdd.inc`. Value
packing (`lx|(lz<<4)|...`, light keys, mob flags) is arithmetic, which is
byte-order independent. Serialization lives in `save.c`, which is replaced.

Note for later: libdragon's flags add `-ffast-math -ftrapping-math
-fno-associative-math` (irrelevant to the integer-only core) and
`-ftrivial-auto-var-init=pattern`, which fills uninitialized locals with a
pattern. That can mask or change behavior that relied on whatever x86 left
on the stack; keep it in mind if a result differs only on the N64.

## 4. Big-endian tests in ares

Each test runs as its own ROM on the emulated VR4300 (big-endian), unchanged
apart from the link. `tests/harness.c` wraps `main` and `exit`
(`-Wl,--wrap=main,--wrap=exit`), captures stdout, and writes the exit code
and log to `g_test_result` in uncached RDRAM. `tools/run_tests.py` starts
the pinned ares with its GDB server on, reads that record with GDB `m`
packets (reads do not pause emulation), and stops ares. ares's server drops
any client whose first byte is not `+` (a guard against HTTP requests to
localhost), so the runner sends `+` first. Test runs use their own ares
settings file with `Input/Defocus=Allow` (ares pauses unfocused windows by
default), the Expansion Pak on, and saves under `build/ares-tests/saves`.
The default per-test limit is 120 s; on a timeout the partial log is kept.

| Test | Covers | Result | Guest time |
| --- | --- | --- | ---: |
| `player_test` | Jump (peak 1.244 blocks, as on the PC), one jump per press, ledge, ceiling, swimming | passed | 0.006 s |
| `mobs_test` | Spawn reserves, night/day, torches, cave floors | passed | 3.79 s |
| `mob_regions_test` | Mob ownership, freezing, regions, slot pressure, deaths, fuses, persistence (mob codec) | passed | 1.97 s |

The two mob tests declare 6.3 MB of static world arrays (256x256 fixtures),
so tests run with 8 MB. That is the fixture's size, not the game's budget.

### Tests deliberately not run on the N64

- **World generation** (`caves_test`): DOScraft does not generate worlds
  in the game. It ships a prebaked 1024x1024 world (seed 4242), baked on the
  PC; on the N64 the bake goes into ROM. Generation is host-side tooling and
  needs no big-endian test. A first run of `caves_test` was stopped: it
  builds 16 worlds up to 256 wide and allocates the whole volume several
  times (about 8.5 MB already at width 160), which cannot fit in 8 MB. The
  runner then had no progress output and a 20-minute limit, so the stall
  looked like slowness; both are fixed above.
- **Towns 256-world cache/stream/edit/light tests** (`column_cache_test`,
  `stream_test`, `edit_test`, `deferred_light_test`): DOScraft's large-world
  overlays (`dos_large_world`, `dos_stream_light`, …) replace much of that
  path. The relevant tests are DOScraft's own, run against DOScraft's staged
  sources. That is extraction work for the next step.
- `keyboard_test` (Towns keyboard driver), save/floppy/HDD tests (storage is
  replaced, Q6) and render pixel tests (x86 painter rasterizer, replaced by
  the RDP).

## Next

Stage DOScraft's actual game source (the Towns core plus its shipped
overlays, including large-world streaming and baked light) for MIPS, and
port the DOScraft tests that cover it (stream, light, chickens, mob codec)
with fixtures that fit in N64 RAM.
