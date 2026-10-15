# Townscraft64: N64 port plan

Status, 2026-10-15: planning draft. No implementation, toolchain or emulator
work has started. Numbers marked *estimate* come from published hardware
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

## 2. Target hardware

| Component | Target | Notes |
| --- | --- | --- |
| Console | NTSC N64 (NUS-001) | PAL timing is a later check, not a release gate. |
| CPU | NEC VR4300, 93.75 MHz, MIPS III, 16 KB I-cache / 8 KB D-cache | Runs as 32-bit code (o32-style pointers); 64-bit ops only where measured useful. |
| RCP | RSP (vector coprocessor, microcode) + RDP (rasterizer), 62.5 MHz | RDP has 4 KB TMEM for textures. |
| RAM | 4 MB RDRAM base; Expansion Pak decision open | See [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) Q2. |
| Cartridge ROM | Up to 64 MB | Size class open; a larger ROM costs nothing on emulator/flashcart but matters for authenticity. |
| Save | FlashRAM 128 KB, SRAM 32 KB, EEPROM 512 B/2 KB, Controller Pak 32 KB | Choice tied to save design, §6. |
| Input | Standard controller; N64 Mouse optional | Controller mapping open, Q5. |
| Video | 320x240, 16-bit color, NTSC | 640x480 not considered for gameplay. |

## 3. Source baseline

DOScraft pinned an immutable Towns snapshot (`86craft/DOScraft/vendor/towns`,
`MANIFEST.json`, source commit `d706a67d`) and kept its own platform adapters
separate. Townscraft64 should do the same: copy a pinned snapshot with a
manifest, never build against whatever is dirty in `fmtowns/townscraft`, and
cherry-pick later fixes on purpose.

Which snapshot is open (Q1). DOScraft has since added a 1024x1024 prebaked
seed, version-5 saves, sampled audio and many PC-specific renderer changes.
The renderer work does not transfer; the world/save/audio-asset work might.

### What carries over

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
| Input | keyboard/mouse/pad in `game.c`/`sys.c` | Controller via the SDK's joybus API. |

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
texturing and Z for free, and the user may prefer to keep the point-sampled
look (Q4).

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

Proposed model (needs approval, Q6):

1. **Base world in ROM.** Prebaked seed(s) stored compressed on the
   cartridge, read by DMA on demand. Replaces Towns' HDD scratch backing and
   DOScraft's prebaked HDD seed. Seed generation on the console is optional;
   the generator still runs at build time.
2. **Edits as a RAM delta.** Changed columns stay in RAM, compressed, on top
   of the ROM base.
3. **Save = delta + player/inventory/mobs** written to FlashRAM (128 KB).
   This caps how much of the world a player can change per save. The cap
   must be measured with real edit patterns and shown to the player.

This is a behavior change from both existing ports (they can save
arbitrarily large edits) and needs user sign-off before Phase A treats it as
parity.

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

- **SDK.** Recommendation: libdragon (open source, GCC toolchain, maintained,
  hardware-accurate RDP command layer). Nintendo's libultra is proprietary
  and only available through leaks; avoid it. Q3.
- **Emulator.** ares for development and measurement; it is the
  low-level emulator the libdragon project recommends. High-level emulators
  give misleading performance and should not be used for numbers. How close
  ares timing is to hardware for our workload must itself be checked.
- **Real hardware.** A flashcart (for example SummerCart64) for final
  measurements, as DOScraft uses 86Box but calibrates against period
  hardware where possible.
- **Host tests.** Keep native logic tests; add the big-endian variant (§3).

## 9. Phases and milestones

| Milestone | Deliverable | Gate |
| --- | --- | --- |
| N0: plan | This document, open questions answered | User signs off on Q1–Q6. |
| N1: toolchain | Pinned SDK and toolchain build, hello-triangle ROM, launch script for ares | Boots in ares and (if available) on hardware; build is reproducible. |
| N2: portable core | Pinned source snapshot, core compiled for MIPS, endian/alignment fixes, big-endian host tests | All existing logic tests pass on big-endian. |
| N3: render slice | Static chunk meshes on the RDP with Z, textures, vertex light; controller look/move | Frame time, triangle and fill counters reported in ares. |
| N4: gameplay | Breaking/placing, inventory, crafting, mobs, day/night, weather, HUD | Feature checklist from the pinned source. |
| N5: world/save | ROM world, RAM delta, cartridge save/load | Save/reload round trip; edit cap measured. |
| N6: audio | Effects and music via RSP mixer | Audible check; mixer cost profiled. |
| N7: parity | Complete feature checklist, measured baseline | User accepts parity. Gates Phase B. |

Phase B candidates (not before N7): float math on the VR4300 FPU, no-Z
opaque terrain, larger view distance, filtered textures, smooth lighting,
Expansion Pak mode, multiplayer split-screen.

No calendar estimate until N1–N3 give real measurements.
