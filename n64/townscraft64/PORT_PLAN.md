# Townscraft64: N64 port plan

Status, 2026-10-15: N1 and N2 done. Pinned toolchain, libdragon and ares
([N1](research/n1-toolchain-2026-10-15.md)); DOScraft snapshot, feature
checklist, Towns core on MIPS and big-endian tests
([N2](research/n2-portable-core-2026-10-15.md)). No playable game code yet. Numbers marked *estimate* come from published hardware
figures or rough reasoning and must be replaced by measurements on the
target before they drive a decision.

## 1. Summary

The N64 is a very different target from the Towns and the PC. The CPU is
roughly 2x a 486DX2 on integer code, but the real change is the RCP:
perspective-correct, filtered, Z-buffered triangles in hardware. The game's
biggest subsystem, the software painter renderer with assembly span loops,
does not port; it is replaced. Gameplay, world generation, inventory,
crafting, mobs and physics are portable C and should carry over largely
intact, subject to big-endian and alignment fixes.

The hard constraints run the other way from the PC port:

- **RAM is small.** 4 MB unified (8 MB with the Expansion Pak), shared with
  framebuffers, Z-buffer, audio and code. Closest existing analog is the
  Towns 2 MB/4 MB profiles, not DOScraft's 8–16 MB.
- **Writable storage is tiny.** Cartridge saves top out at 128 KB FlashRAM
  (or 32 KB SRAM / 32 KB Controller Pak). The Towns save floppy is 1232 KB
  and DOScraft writes a world directory plus edited terrain slots to HDD.
- **Read-only storage is large and fast.** Cartridge ROM up to 64 MB with
  random-access DMA and no seek time. A prebaked world fits the medium far
  better than it fit a period floppy.

Follow DOScraft's structure: Phase A is a direct port to parity, Phase B is
separate N64-specific improvement and only starts after the user accepts
parity.

Project philosophy (user, 2026-10-15): squeeze out every last millisecond.
Choices between otherwise-equal options go to whichever leaves more room
for hand-tuned code, and every performance claim needs a measurement.

## 2. Target hardware

| Component | Target | Notes |
| --- | --- | --- |
| Console | NTSC N64 (NUS-001) | PAL timing is a later check, not a release gate. |
| CPU | NEC VR4300, 93.75 MHz, MIPS III, 16 KB I-cache / 8 KB D-cache | Runs as 32-bit code (o32-style pointers); 64-bit ops only where measured useful. |
| RCP | RSP (vector coprocessor, microcode) + RDP (rasterizer), 62.5 MHz | RDP has 4 KB TMEM for textures. |
| RAM | 4 MB base runs the full game; 8 MB Expansion Pak unlocks extras | Q2, decided 2026-10-15. The Expansion Disk path requires 8 MB. |
| Cartridge ROM | 64 MB maximum (Q7) | Period hardware only: nothing Nintendo did not ship (through ~2000, Japan). |
| Save | FlashRAM 128 KB (Q6); optional 64DD Expansion Disk | Cartridge is the complete game; the disk path needs the Expansion Pak. See §6. |
| Input | Official: N64 Mouse (NUS-005) + Randnet keyboard. Fallback: standard controller, Turok-style | Q5. Keyboard needs our own Joybus driver; emulator support unconfirmed. |
| Video | 320x240, 16-bit color, NTSC | 640x480 not considered for gameplay. |

## 3. Source baseline

**Reference game: current DOScraft** (Q1, decided 2026-10-15). Phase A
parity means DOScraft's feature set on the N64.

DOScraft is built from an immutable Towns snapshot
(`86craft/DOScraft/vendor/towns`, `MANIFEST.json`, source commit
`d706a67d`) plus its own code in `src/platform/dos/` and offline tools in
`tools/`. Townscraft64 follows the same discipline: import a pinned DOScraft
revision with a manifest (file hashes and source commit), never build
against the live `86craft/DOScraft` tree, and bring later DOScraft changes
over as reviewed cherry-picks.

Three layers, treated differently:

1. **Towns core** (`vendor/towns/src`): portable gameplay and world code;
   import as-is, same as DOScraft does. Tables below.
2. **DOScraft game features** in `src/platform/dos/` and `tools/`: chickens,
   biomes, resolution-selected textures, first-person arm/held items,
   20-minute day, mob despawn rule, large prebaked streamed world, V3 edit
   log, distant terrain, graphics menus, MIDI music, sound bank. These are
   interleaved with DOS/VGA/IRQ/SB code. Each gets an extraction step: split
   the platform-neutral part into a shared module (or reimplement it for the
   N64) with a test that checks it against DOScraft's behavior.
3. **DOScraft platform and 486 renderer code** (VGA transport, Mode X,
   `f2b_*`, `packed_raster`, `low_planar.S`, IRQ/SB/OPL drivers): not
   ported. Their *design* (for example distant-terrain levels) is the
   reference; the N64 implementation is new.

Feature inventory: N2 starts with a complete DOScraft feature checklist at
the pinned revision, each item tagged core / extract / reimplement, as
DOScraft's own Phase A did for Towns.

Hard fits on the N64, to resolve with Q2 and Q6:

- **Large world.** DOScraft streams a 1024x1024 prebake from a writable
  HDD with 8–16 MB RAM. On the N64 the prebake moves to cartridge ROM
  (fits the medium well); the resident working set must fit ~2.5–3 MB.
- **Edit log.** DOScraft's V3 edit log is written to HDD. The N64 has at
  most 128 KB of writable save memory.
- **Music.** DOScraft's multi-song OPL2 player becomes sampled-instrument
  sequencing on the RSP mixer; the MIDI import pipeline and per-song
  settings remain the source data.

### What carries over from the Towns core

| Area | Towns files | N64 treatment |
| --- | --- | --- |
| Blocks, items, inventory, crafting | `blocks.c`, `inventory.c`, `ui.c` (logic) | Reuse. |
| Player, physics, collision | `player.c`, `physics.c` | Reuse; existing host tests (`player_test`, `keyboard_test`) still apply. |
| World generation, lighting | `world.c` | Reuse, but memory layout must fit §5. |
| Mobs | `mobs.c` | Reuse. Entity drawing moves to the RDP path. |
| Fixed-point math, tables | `fmath.c`, `tools/gentables.py` | Reuse in Phase A. Float conversion is a Phase B experiment (VR4300 FPU is fast, but keep parity first). |
| Textures | `textures.c` | Keep generation; change output format (§4). |
| Game loop | `game.c` | Reuse with a new platform layer. |

### What gets replaced

| Area | Towns files | Why |
| --- | --- | --- |
| Boot, startup, linker | `crt0.S`, `kernel.ld`, `isr.S`, `sys.c` | N64 boot via the SDK's IPL3/entry code. |
| Software raster | `raster.c`, `trap.S` | RDP draws triangles. |
| Painter renderer | `render.c` (most of it) | Mesh building and culling survive; sorting, projection and span drawing become display lists. |
| Video, page flipping | `video.c`, `gfx.c` | SDK display/framebuffer management. HUD and font drawn as RDP rectangles or a CPU blit. |
| Sound | `sound.c` | YM2612/RF5c68 → RSP mixer (§7). |
| Storage | `hdd.c`, `save.c`, `column_*.inc` | ROM + RAM delta + cartridge save (§6). |
| Input | keyboard/mouse/pad in `game.c`/`sys.c` | Randnet keyboard (own Joybus driver), N64 Mouse and controller via libdragon's Joybus/joypad API. |

### Port hazards to check first

- **Endianness.** The N64 is big-endian; Towns and PC are little-endian.
  Audit save codecs, packed structs, `gentables.py` output, any
  byte-reinterpreting casts, and binary assets.
- **Alignment.** MIPS traps on unaligned halfword/word loads. x86 code that
  casts `uint8_t *` to wider types, or packed structs, will fault.
- **`char` signedness** and integer promotions under GCC for MIPS.
- **Host tests.** Existing native tests are built `-m32` for x86. Add a
  big-endian build of the same tests (for example under `qemu-mips`) so
  logic regressions are caught off-console.

## 4. Rendering

Phase A goal: the same world, view distances, HUD and visible content,
drawn by the RDP. Not pixel parity: the N64 gives perspective-correct
texturing and Z for free. Style (Q4, decided 2026-10-15): DOScraft look in
Phase A — point sampling, per-face light, RDP anti-aliasing off. Filtering,
fog, smooth light and similar are Phase B presets, each kept only if it
earns its measured cost.

Mapping of existing techniques:

| Towns technique | N64 equivalent |
| --- | --- |
| Greedy-merged face quads | Keep. Two triangles per quad. Texture repeat across a merged quad uses RDP tile wrapping (power-of-two textures, one tile per block texture, not an atlas). |
| Affine subdivision near the camera | Drop. RDP is perspective-correct. |
| Painter's order, no depth buffer | Z-buffer for correctness. Z costs RDRAM bandwidth, so keep front-to-back opaque order to cut overdraw, and measure the no-Z option for opaque terrain as Phase B. |
| Palette ramps, lighting by subtraction | Vertex shade × texture in the color combiner. Per-vertex light is close to the current per-face light. |
| Texel-0 cutouts (glass, foliage) | Alpha compare in the RDP; cutouts drawn in a separate pass. |
| Distance-preset flat color faces | Untextured shaded triangles; cheaper fill mode. |
| Frustum, backface, group culling | Keep on the CPU; reject whole chunk groups before building display lists. |

Budgets to measure early (all *estimates*):

- **Fill.** 320x240 16-bit with Z is ~77k pixels per layer. Shared RDRAM is
  the usual limit, not the RDP clock. Overdraw from house interiors is the
  known worst case from the Towns house benchmark.
- **Geometry.** Commercial games managed a few thousand triangles per frame.
  The Towns mesh pool holds 16,000–24,000 quads, so the view distance and
  quad pool must be measured, not assumed.
- **TMEM.** 4 KB. 16x16 textures are 512 B in 16-bit, 256 B as CI8, 128 B
  as CI4 plus palette. Sorting draws by texture avoids reloading TMEM.
- **Microcode.** Choice depends on the SDK (Q3).

## 5. Memory budget, 4 MB base (*estimates*)

| Item | Size |
| --- | ---: |
| Two framebuffers, 320x240x16 | 300 KB |
| Z-buffer | 150 KB |
| Code + static data | 300–500 KB |
| Audio buffers + samples | 100–300 KB |
| Display lists (double-buffered) | 100–200 KB |
| Textures and palettes | < 50 KB |
| **Remainder for world, meshes, mobs** | **~2.5–3 MB** |

For comparison, the Towns 4 MB profile runs a 160x160 world. Block plus
light data for a 160x160x48 world is 2.4 MB uncompressed, so whole-world
residency at that size does not fit next to the framebuffers. The existing
25-column cache plus a ROM-backed world (§6) is the natural fit.

## 6. World and saves

Model (Q6, decided 2026-10-15: cartridge is the complete game; an
optional 64DD Expansion Disk is added later, see below):

1. **Base world in ROM.** Prebaked seed(s) stored compressed on the
   cartridge, read by DMA on demand. Replaces Towns' HDD scratch backing and
   DOScraft's prebaked HDD seed. Seed generation on the console is optional;
   the generator still runs at build time.
2. **Edits as a block-level RAM store.** Unlike DOScraft, which pages whole
   edited columns out to HDD, an edited column leaving the resident window
   keeps only its changed blocks (~3 bytes each, or the RLE column if
   smaller) in a fixed RAM budget. Page-in = ROM column + apply its edits.
3. **Save = that edit store + player/inventory/mobs** written to FlashRAM
   (128 KB). One budget for RAM and save (*estimate* ~40,000 changed
   blocks), shown to the player.

This is a behavior change from DOScraft (effectively unlimited edits), so
the edit volume of real play must be measured at N5 against the budget.

**Optional Expansion Disk** (Q6 amendment, 2026-10-15). Modeled on the
F-Zero X Expansion Kit: with a 64DD and Expansion Pak attached, the game
saves several worlds to the disk and pages edited columns out to it as
DOScraft does to its HDD, removing the edit cap. Rules: no cost on the
cartridge path (switch only at column page-in/page-out and save; separate
ROM or overlay), no frame hitches from disk writes (buffer and write at
save/idle), and its own milestone after cartridge saves work.

## 7. Audio

The N64 has no FM or PCM chip; audio is mixed in software on the RSP.

- **Effects.** Use the sampled effects (DOScraft already produces portable
  unsigned mono PCM from the original synthesis). Resample to the N64 mixer
  format.
- **Music.** The Towns score drives a YM2612 FM piano; DOScraft adapts it to
  OPL2. On the N64, render the instrument to samples and sequence it, or use
  a tracker format the SDK supports.
- Positional volume/pan is direct; the mixer is stereo.

## 8. Toolchain and test platforms

- **SDK: libdragon** (Q3, decided 2026-10-15). Pin the libdragon revision
  and toolchain like DOScraft pins DJGPP. The project goal is maximum
  performance, so the 3D layer (GL port, tiny3d or custom microcode) is
  picked by measurement, and custom RSP microcode is in scope.
- **Emulator: ares is the target** (Q7, decided 2026-10-15). Development,
  measurement and acceptance all run in a pinned ares build, as DOScraft
  targets pinned 86Box VMs. High-level emulators give misleading
  performance and are not used for numbers. ares v148 has no Randnet
  keyboard (N1 finding; ares issue #1482), so a pinned ares fork with a
  keyboard device is required before the official controls can be tested.
- **Real hardware:** not an acceptance gate. Only period hardware features
  are used (no flashcart SD/USB/extra RAM), so the ROM should still run on
  a console, but this is not verified.
- **Host tests.** Keep native logic tests; add the big-endian variant (§3).

## 9. Phases and milestones

| Milestone | Deliverable | Gate |
| --- | --- | --- |
| N0: plan | This document, open questions answered | User signs off on Q1–Q6. |
| N1: toolchain — **done 2026-10-15** | Pinned libdragon/toolchain build, pinned ares, hello-triangle ROM, launch script; check ares Mouse, Randnet keyboard and 64DD support, and libdragon 64DD support | Boots in ares on 4 MB and 8 MB. Keyboard: needs an ares fork. 64DD: ares emulates it; libdragon has no disk driver. Mouse works in ares (user-verified). |
| N2: portable core — **done 2026-10-15** | Pinned DOScraft snapshot with manifest, DOScraft feature checklist (core / extract / reimplement / drop), Towns core compiled for MIPS, big-endian tests in ares | Core compiles (no endian issues outside the replaced HDD code); player and mob tests pass big-endian. World generation and the Towns 256-world tests are out of scope (DOScraft ships a prebaked world). See research/n2-portable-core-2026-10-15.md. |
| N2b: DOScraft source | DOScraft's game source (Towns core plus shipped overlays, incl. large-world streaming and baked light) staged and compiled for MIPS; DOScraft's tests for it ported with N64-sized fixtures | Before N3's world rendering. |
| N3: render slice | Static chunk meshes on the RDP with Z, textures, vertex light; controller look/move | Frame time, triangle and fill counters reported in ares. |
| N4: gameplay | Breaking/placing, inventory, crafting, mobs, day/night, weather, HUD | Feature checklist from the pinned source. |
| N5: world/save | ROM world, RAM delta, cartridge save/load | Save/reload round trip; edit cap measured. |
| N6: audio | Effects and music via RSP mixer | Audible check; mixer cost profiled. |
| N7: parity | Complete feature checklist, measured baseline | User accepts parity. Gates Phase B. |
| N8: Expansion Disk | 64DD driver (libdragon's or ours), multi-world saves, edited-column page-out to disk, buffered writes | Cartridge-path frame times and I-cache misses unchanged; save/reload round trip on disk in ares; worst-case disk hitch measured. Can start once N5 works; does not gate N7. |

Phase B candidates (not before N7): float math on the VR4300 FPU, no-Z
opaque terrain, larger view distance, filtered textures, smooth lighting,
Expansion Pak mode, multiplayer split-screen.

No calendar estimate until N1–N3 give real measurements.
