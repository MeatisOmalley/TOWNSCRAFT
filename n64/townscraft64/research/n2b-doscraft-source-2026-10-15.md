# N2b DOScraft game source on MIPS (2026-10-15)

## 1. Re-pin to DOScraft c4ce3b7b

DOScraft master moved during the day: 6-bit block IDs (wall torches and open
doors split out), large-world save version 6, and a design note
(`EARLY_GAME.md`: clay, copper, bronze, spear, hunger; documentation only).
`vendor/doscraft` was re-imported at `c4ce3b7b` ("record save version 6 and
the block-ID release") before any extraction depended on the 5-bit block
format. The Towns snapshot inside it is unchanged (`d706a67d`). The import
now also carries `EARLY_GAME.md` and ten DOScraft test files (five tests and
their drivers). 379 files, all matching the manifest.

## 2. Shipped source, staged

`tools/stage_doscraft.py` runs DOScraft's own `build_game.build()` from the
pinned import with the shipped flag set (build defaults plus
`DOS_LARGE_WORLD`, `DOS_STREAM_LIGHT_CACHE` and the four `DOS_FAR_*` flags,
graphics presets and audio on), stops it at the point where DOScraft would
call its DOS compiler, and captures the overlay transform. The sound bank and
music pack builders are stubbed (data files, not source). Output:
`build/doscraft/imported/src` (Towns core + overlays, 21 of 38 files
overlaid), `build/doscraft/platform/src`, `stage.json`, `defines.mk`.

Verification against DOScraft's real shipped build
(`86craft/DOScraft/build/game/386sx25`, built from these commits on
2026-10-15): **39 of 39 imported source files and 43 of 43 platform files
are byte-identical.** The only differences are `libc.c` (our N64 change
below) and `tables.c` (generated at compile time in both).

## 3. N64 adaptations

`tools/n64_adapt.py`, used by every staging tool, recorded per file:

| File | Change |
| --- | --- |
| `libc.c` | Remove x86 `rep`-assembly `memset`/`memcpy`/`memmove`/`memcmp`/`strlen`; newlib supplies them. |
| `fmath.h` | `mulshift`/`divshift`/`muldiv` x86 inline assembly (`imull`, `shrdl`, `idivl`) → C with 64-bit intermediates. Same results for shifts 1–31; every shipped call uses 16 or 4. On the VR4300 these are `dmult`/`ddiv`, a Phase B performance item. |

**Correction to N2:** the N2 report said all 13 core files compiled. The
list it gave had 14 names, and `render.c` had in fact failed to build: the
`fmath.h` assembly only fails in files that call it, and the assembler's
"Error:" lines were filtered out by a case-sensitive search. The fmath
adaptation fixes it; the Towns core now builds 14 of 14, and builds now
check make's exit status instead of searching output.

## 4. Compile for MIPS

`python tools/build.py doscraft` compiles the 14 shipped game files (all
but `raster.c`, `abi_check.c`, `trap.S`) with DOScraft's game defines and
`vendor/doscraft/src/platform/dos` on the include path, as DOScraft does.
Result: **14 of 14 compile, make exits 0.** Warnings worth tracking:

| Warning | Where | Assessment |
| --- | --- | --- |
| `-Wcast-align` ×72 | `far_fill.h` (included by `render.c`) | CPU span fills of the distant-terrain renderer, writing `u32`s through byte pointers. Would trap if misaligned; this renderer is reimplemented for the RDP anyway. |
| `-Wcast-align` ×4 | `column_hdd.inc` | Terrain medium header and index read as native `u32` (see §6). |
| `-Wcast-align` ×1 | `stream_light_cache.inc:18` | Lighting index read as native `u32` (see §6). |
| unused / sign-compare | `world.c`, `render.c`, `far_*` | Benign. |

## 5. DOScraft tests, big-endian in ares

`tools/stage_doscraft_tests.py` stages each test exactly as its DOScraft
driver (`vendor/doscraft/tests/test_*.py`) does, applies the N64
adaptations, and writes ROM targets (`build/dtests/tests.mk`) and expected
outcomes (`expect.json`). `tests/harness.c` now passes a case name as
`argv[1]` (`-DTEST_ARG`), so each mob-codec case is its own ROM.
`python tools/run_tests.py --build` runs both suites.

| ROM | Covers | Expected | Result | Guest time |
| --- | --- | --- | --- | ---: |
| `player_test` (Towns) | jump, ledge, ceiling, swim | pass | passed | 0.006 s |
| `mobs_test` (Towns) | spawn reserves, day/night, torches, cave floors | pass | passed | 3.79 s |
| `mob_regions_test` (Towns) | ownership, freezing, regions, codec | pass | passed | 1.97 s |
| `dt_chickens` | chicken spawn, model, physics, drops, inventory, save IDs | pass | passed | 0.045 s |
| `dt_mob_codec_*` ×7 | roundtrip, empty capacity, malformed headers/records/ranges, boundaries, truncation | pass | 7 passed | ≤0.031 s |
| `dt_immediate_light_baseline` | original Towns `world.c` | must fail "placed torch lighting was deferred" | passed (failed as expected) | 3.24 s |
| `dt_immediate_light_fixed` | DOScraft's overlaid `world.c` | pass | passed | 3.25 s |

**13 of 13 as expected.** The immediate-light fixture uses Towns' 4 MB
profile (a 160-wide world generated in the test, about 3.2 s on the VR4300).

Deviation from DOScraft: `test_immediate_light.py` overlays `world.c`
alone. At `c4ce3b7b` that no longer compiles, because the block-ID overlay
makes `world.c` use `BLOCK_IDS` from the overlaid `blocks.h`; DOScraft's own
test would fail too (it skips when its Zig toolchain is absent). The port
applies the default overlays to every staged file for the "fixed" variant.

## 6. Not yet run: tests that need the baked 1024 world

`stream_light_native.c` (baked light versus canonical flood fill, cold
eviction, seam and edit invalidation) and `far_world_native.c` read the
real baked medium through DOScraft's sector backend (`hdd.c`, 156 lines of
DOS file I/O). Running them on the N64 needs three things:

1. **A sector backend for the N64.** Reads from the medium in cartridge
   ROM; writes (dirty-column eviction, edit records) into a RAM overlay. The
   compact V2/V3 format is decoded by the world code (`large_world.inc`),
   not by `hdd.c`, so the backend stays small. This is the seed of N5's ROM
   world.
2. **A current bake.** The only compact medium on disk
   (`terrain1024-crown-leaves-v2.bin`, 33,835,008 bytes, 2026-10-14) predates
   the 6-bit block-ID change. A fresh bake from `c4ce3b7b`'s tools is needed.
   The uncompressed media (178 MB) do not fit a 64 MB cartridge; the compact
   one does.
3. **A byte-order decision.** The medium is written little-endian on the PC,
   and its header, column index and lighting index are read as native `u32`
   (`column_hdd.inc`, `stream_light_cache.inc`). On the big-endian N64 every
   such value would be byte-swapped. Column payloads are byte streams and
   checksums are byte-wise, so only these word tables are affected. Options:
   - **Convert at ROM build time:** an N64 bake step rewrites the word tables
     big-endian. Game code stays identical to DOScraft and costs nothing at
     run time, but the converter must know every word field in the format and
     track DOScraft format changes.
   - **Read little-endian in the game:** an N64 overlay replaces those loads
     with `le32()` reads. Robust to format changes; touches a handful of
     sites; the cost is negligible because these tables are read at load time.

Also: the test allocates DOScraft's 9 MiB arena (1 MiB low + 8 MiB high,
`g_ramMB=8`). An N64-sized fixture uses DOScraft's 4 MiB configuration (the
386 build runs the 1024 world in 4 MiB) with smaller arenas.
