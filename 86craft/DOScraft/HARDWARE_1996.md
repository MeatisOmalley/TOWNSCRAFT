# 1996 DOScraft: Pentium 200 and 3dfx Voodoo Graphics

A separate port of the current game to a top-end 1996 consumer/workstation
PC, with two new renderers. The game itself is unchanged: the same release
gameplay, large world, far field and audio, built for the Pentium.

**Status:** both renderers build and pass native x86 tests. They have **not
run in 86Box yet**. The 1996 VM profile, its ROMs, the two `.cmd` launchers
and in-emulator frame-rate measurements are the next step. No FPS claims are
made here.

## Target machine

| Part | Choice | 86Box device |
| --- | --- | --- |
| CPU | Intel Pentium 200 (P54C, 66 MHz bus x3) | Pentium 200 |
| Motherboard | ASUS P/I-P55T2P4, Intel 430HX, 512 KiB pipeline-burst L2 | `p55t2p4` |
| RAM | 128 MiB EDO (the 430HX caches up to 512 MiB) | 131072 KiB |
| 2D video | Matrox Millennium, 4 MiB WRAM, PCI | Matrox Millennium |
| 3D video | 3dfx Voodoo Graphics, 4 MiB (2 MiB frame buffer, 2 MiB texture), Diamond Monster 3D class | `voodoo` (Voodoo Graphics) |
| Sound | Creative Sound Blaster AWE64 Gold (SB16 and OPL3 compatible) | `sbawe64_gold` |
| Storage | Adaptec AHA-2940UW (AIC-7880, Ultra Wide SCSI) with a 7200 rpm workstation drive (Seagate Barracuda class) | AIC-7880 |

This is top-end consumer/workstation hardware, not server hardware: one CPU,
EDO rather than server memory, and a desktop SCSI drive rather than an array.
The Pentium was picked over the Pentium Pro because the render loops use
Quake's P5 technique (see below), and the P6 penalizes the byte-register
stepping those loops rely on. Release dates and exact drive and ROM models
still need sources, as the 1990 and 1992 profiles have, when the VM is pinned.
The Sound Blaster PCM effects and OPL FM music work unchanged on the AWE64.
The disk image keeps the current prebaked world size; it does not model the
drive's full capacity.

## Executables

```powershell
python tools/build_1996.py              # all three, into build/game/p5-200/<variant>
python tools/build_1996.py soft 3dfx    # just the new renderers
```

| File | Renderer | Display |
| --- | --- | --- |
| `D96REF.EXE` | the shipping 386/486 renderer, byte-identical build configuration | Mode X (any VGA) |
| `D96SOFT.EXE` | Pentium software renderer | Mode X (any VGA) |
| `D963DFX.EXE` | Voodoo Graphics renderer, own SST-1 driver | Voodoo 640x480, 16-bit |

`D96REF` is the baseline for measuring the gain. The two `.cmd` launchers
will boot the same VM with and without the Voodoo enabled.

## Renderer architecture

Both new renderers keep the release 3D front end: `render.c` with all of its
overlays (collection, painter's order, culling, far field, viewmodel). The
painter's order is exact for this voxel world, so neither renderer needs a
depth buffer. One new overlay, `tools/dos_r96.py`, makes each projected
vertex carry `w` (the projection's own `focal<<16/z`). It also clips that
perspective-correctly in the guard band, flags 3D polygons `RP_PERSP`,
enlarges the draw list, reports atlas rebuilds, and on 32 MiB or more keeps
a 7x7 ring of terrain columns resident (see Presets). `raster.c` and
`trap.S` are replaced per executable.

### Software (`src/platform/r96/r96_soft.c`, `r96_span.S`, `r96_vga.S`)

Pixel coverage is identical to the 486 rasterizer. Texturing is now
perspective-correct where the 486 build used screen-affine spans with near
subdivision:

- Per polygon, the plane of q=1/z, s=u*q and t=v*q is fitted once on the
  FPU. Edge slopes come from a reciprocal table and one IMUL (about 14
  cycles) instead of IDIV (46).
- **Short spans (up to 16 pixels, about half of all spans at long views)**
  use the exact value and slope at the span's middle. This takes one
  reciprocal, with the same error bound as an exact-ends chord and no inner
  seam.
- **Longer spans** are cut into 16-pixel segments, exact at the ends and
  linear in between (Quake's scheme, seamless across segments). Full
  segments step by an integer subtract and shift, so only their end values
  pass through FIST (6 cycles, unpaired). 16-pixel endpoints come from
  precomputed 16-pixel plane steps (adds only).
- **Carmack's FDIV overlap:** the Pentium's FDIV runs for 39 cycles (19 at
  the single precision used here) while the integer pipes keep issuing.
  - The divide for the next segment's end starts before the current
    segment's integer texel loop.
  - During a row's last segment, the next row's divide starts instead (at
  the middle of that row's span when it is short), so short rows never wait
    for the FPU.
  - A long span's first segment end, the one divide it still waits for, runs
    under that segment's integer setup.

  86Box's Pentium timing models the overlap (`FPU_CYCLES(39,38,2)` for
  FDIV). Precision is set once per frame (FLDCW costs 8 cycles).
- The texel loop jumps into a 16-pixel unrolled block (no loop counter). It
  steps u and v with byte carries, without the 486 loop's 16-bit prefixed
  add, which the P5 cannot pair: the v fraction rides in the upper half of
  EAX. It is AGI-bound at about 4 cycles per pixel.
- **No write-allocate on the P54C:** byte stores to frame-buffer lines that
  are not in L1 drain one by one through two write buffers. Each segment,
  and each flat or sky fill of 32 bytes or more, first reads its
  destination lines so the stores hit L1. This gains on real hardware only;
  86Box does not model caches, and costs it about two loads per segment.
- **Presentation:** Mode X at full resolution now packs each plane's four
  pixels into one dword and stores it straight into VGA memory, in 16-row
  bands that stay in L1 across the four planes. That replaces a byte-wise
  deinterleave into a buffer followed by a copy. 86Box charges the
  Millennium one PCI clock per 32-bit write and two per byte write. The HUD
  keeps its per-page change cache.
- The scale-2 presets and the tests use a C reference of the same
  arithmetic; the assembly matches it bit for bit. The held item (no `w`)
  keeps the old affine mapping.

### Voodoo Graphics (`src/platform/r96/r96_raster3dfx.c`, `r96_sst1.c`, `r96_gfx3dfx.c`)

An in-house SST-1 driver programs the board directly, instead of Glide 2. It
handles PCI discovery, a 640x480 RGB565 double-buffered mode (no depth
buffer), P8 textures, float parameter registers, fast fills, LFB writes and
swaps. The host-side triangle setup is specialized for this game:

- Every tile is the game's pre-lit 16x16 palettized texel block, uploaded
  once as a P8 texture. No per-pixel lighting is computed, and the look
  (point sampling, same palette) is unchanged. Texel 0 stays transparent
  through the chroma key.
- One plane setup per polygon. Fans from the topmost vertex share vertex A,
  its start values and all gradients, so later triangles send only two
  vertices and the command.
- Registers go out in ascending order (A, B, C, starts, gradients, command)
  for PCI bursts, and unchanged state is never resent: about 22 writes per
  textured face on real frames.
- The plane's one FDIV overlaps the first triangle's six vertex writes,
  which are integer moves of the float bits (an FPU store would wait for
  the divide); the disassembly confirms the order.
- 3D renders at 640x400; one logical pixel of the 320x200 front end is a
  2x2 block, and the textures are perspective-correct per pixel.
- The game's 2D layer (HUD, text, crosshair, rain, menus) still draws into
  its 8-bit pages.
  - 3D frames leave index `0xF0` (olive level 0, which the UI never draws)
    in the view rows, and only other pixels are written over the Voodoo
    frame.
  - A page is re-keyed only in the rows its overlays touched.
  - HUD and 2D-frame rows are diffed per Voodoo buffer.
  - Darkening a 3D frame for a menu first reads the view back through the
    LFB.
- Distant terrain draws into an 8-bit layer, sent as chroma-keyed 64x64
  texture tiles. Each tile is drawn only over the rectangle its content
  spans (chroma-keyed pixels still cost fill) and downloaded only when its
  texels changed: a still view goes from about 14,000 texture writes per
  frame to almost none.
- Dynamic textures (held-item icon, far tiles) alternate between two TMU
  slots, so queued triangles never see a rewrite.

The driver follows 86Box's SST-1 model. A real board also needs Glide's
`sst1Init` DRAM, graphics-clock and DAC detection; that, or porting Glide as
an alternative, is future work.

### Presets in the 1996 build

All presets run at 320x200 (Very Low at 160x100) with no near subdivision.
Low/Medium/High view 8/16/24 blocks, and High is the startup default; the
settings menu reaches 32. Distant terrain starts at High.

On 32 MiB or more (the 128 MiB machine), the world keeps a 7x7 ring of
terrain columns resident: at least 48 blocks on every side, for a 32-block
view plus the streaming margin. It also gets a 64,000-quad mesh pool and a
2 MiB low / 24 MiB high heap tier. The 386/486 builds keep 5x5 and 24,000
quads.

### Measured workload

`tests/r96_frame_probe.c` renders the real prebaked world through the
actual transformed front end, natively: 20 views from five places, four
directions each, at eye height and from above. Per frame, on average:

| View | Faces | Rows | Spans | Textured pixels | Segments |
| --- | --- | --- | --- | --- | --- |
| 8 | 34 | 1,108 | 1,022 | 36k | 2,781 |
| 16 | 162 | 2,987 | 2,802 | 59k | 5,186 |
| 24 | 409 | 5,335 | 4,950 | 77k | 7,543 |
| 32 | 795 | 7,849 | 7,202 | 91k | 9,949 |

Per-row overhead outweighs per-pixel work at these settings, which is what
the short-span path and the FDIV scheduling target. Host timings say
nothing absolute about a Pentium; real frame rates need the VM.

## Verification so far

`python -m unittest tests.test_r96` (native x86 via the pinned Zig, plus
DJGPP compiles):

- **Overlay:** applies exactly to the release sources.
- **Software (3000 random projected quads):**
  - coverage equals the flat rasterizer's, with no writes outside the polygon
    or the target;
  - the assembly matches the C reference bit for bit;
  - every pixel is within one texel of the span model (exact ends or middle
    with linear steps);
  - transparency is respected, and the FPU control word and x87 stack are
    restored after each frame;
  - the Mode X plane writer equals a byte-wise pack.
  - On these deliberately harsh scenes (near, grazing), 5.9% of unminified
    texels land on a neighbor of the exact per-pixel texel: the usual cost of
    piecewise-linear spans.
- **Voodoo (1500 scenes against a model of the SST-1 that follows 86Box's
  span walk, start-value and S/W sampling rules):**
  - 99.95% of texels exact;
  - edges within 1.7% of the 320x200 coverage (the board draws true 640x400
    edges);
  - fast fills, the far-terrain layer and affine polygons checked.
- **Real frames:** the prebaked world (with its distant-terrain map) through
  both builds' front ends.
  - The software assembly equals the C reference on all 20 views at 16 and
    32 blocks.
  - The Voodoo backend renders all 20 views at 24 blocks into the model.
  - Images were inspected.

The release build is byte-identical after the shared build changes.

## Next steps

1. Create the 1996 VM profile: pinned ROMs for the P/I-P55T2P4 and the
   Millennium, CMOS, a private HDD with the prebaked world, plus
   `ISO/Launch-DOScraft-1996.cmd` and `ISO/Launch-DOScraft-1996-3dfx.cmd`.
2. In-emulator smoke tests of all three executables, including the Voodoo
   mode set, compositing and menu read-back.
3. Frame-rate benchmarks of `D96REF` vs `D96SOFT` vs `D963DFX` on the fixed
   replays at 16, 24 and 32 blocks. Then tune the presets and profile the
   hot loops under 86Box's Pentium timing.
4. Glide or a real-board `sst1Init`.
