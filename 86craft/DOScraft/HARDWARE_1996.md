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
enlarges the draw list for 16-block views and reports atlas rebuilds.
`raster.c` and `trap.S` are replaced per executable.

### Software (`src/platform/r96/r96_soft.c`, `r96_span.S`)

Pixel coverage is identical to the 486 rasterizer. Texturing is now exactly
perspective-correct where the 486 build used screen-affine spans with near
subdivision:

- Per polygon, the plane of q=1/z, s=u*q and t=v*q is fitted once on the FPU.
- Each span is cut into 16-pixel segments, exact at the ends and linear in
  between.
- **Carmack's FDIV overlap:** the Pentium's FDIV runs for 39 cycles (19 at the
  single precision used here) while the integer pipes keep issuing. The divide
  for the next segment's end is started before the integer texel loop of the
  current segment. During a span's last segment, the divide for the next
  row's start is issued instead. A row therefore normally stalls on only one
  divide. 86Box's Pentium timing models this overlap (`FPU_CYCLES(39,38,2)`
  for FDIV).
- The texel loop jumps into a 16-pixel unrolled block (no loop counter). It
  steps u and v with the 486 build's byte carries, but without its 16-bit
  prefixed add, which the P5 cannot pair: the v fraction rides in the upper
  half of EAX.
- The scale-2 presets and the tests use a C reference of the same algorithm.
  The held item (no `w`) keeps the old affine mapping.

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
  vertices and the command. Registers go out in ascending order for PCI
  bursts, and unchanged state is never resent. The model test measures 23
  register writes per textured quad, including texture switches.
- 3D renders at 640x400; one logical pixel of the 320x200 front end is a
  2x2 block, and the textures are perspective-correct per pixel.
- The game's 2D layer (HUD, text, crosshair, rain, menus) still draws into
  its 8-bit pages. 3D frames leave index `0xF0` (olive level 0, which the UI
  never draws) in the view rows, and only other pixels are written over the
  Voodoo frame. HUD and 2D-frame rows are diffed per Voodoo buffer. Darkening
  a 3D frame for a menu first reads the view back through the LFB.
- Distant terrain draws into an 8-bit layer, sent as chroma-keyed 64x64
  texture tiles.
- Dynamic textures (held-item icon, far tiles) alternate between two TMU
  slots, so queued triangles never see a rewrite.

The driver follows 86Box's SST-1 model. A real board also needs Glide's
`sst1Init` DRAM, graphics-clock and DAC detection; that, or porting Glide as
an alternative, is future work.

### Presets in the 1996 build

All presets run at 320x200 (Very Low at 160x100) with no near subdivision.
Low/Medium/High view 8/12/16 blocks, and High is the startup default. Distant
terrain starts at High. The renderer's 16-block limit stays: going further
means larger world streaming, to be decided from measurements. The 128 MiB
machine gets a 2 MiB low and 24 MiB high heap tier.

## Verification so far

`python -m unittest tests.test_r96` (native x86 via the pinned Zig, plus
DJGPP compiles):

- **Overlay:** applies exactly to the release `render.c`/`raster.h`.
- **Software (3000 random projected quads):**
  - coverage equals the flat rasterizer's, with no writes outside the polygon
    or the target;
  - the assembly matches the C reference on 99.999% of pixels;
  - every pixel is within one texel of exact-at-segment-ends with linear
    steps between;
  - transparency is respected, and the FPU control word and x87 stack are
    restored.
  - On these deliberately harsh scenes (near, grazing), 6.1% of unminified
    texels land on a neighbor of the exact per-pixel texel: the usual cost of
    16-pixel segments.
- **Voodoo (1500 scenes against a model of the SST-1 that follows 86Box's
  span walk, start-value and S/W sampling rules):**
  - 99.95% of texels exact;
  - edges within 1.7% of the 320x200 coverage (the board draws true 640x400
    edges);
  - fast fills, the far-terrain layer and affine polygons checked.

The release build is byte-identical after the shared build changes.

## Next steps

1. Create the 1996 VM profile: pinned ROMs for the P/I-P55T2P4 and the
   Millennium, CMOS, a private HDD with the prebaked world, plus
   `ISO/Launch-DOScraft-1996.cmd` and `ISO/Launch-DOScraft-1996-3dfx.cmd`.
2. In-emulator smoke tests of all three executables, including the Voodoo
   mode set, compositing and menu read-back.
3. Frame-rate benchmarks of `D96REF` vs `D96SOFT` vs `D963DFX` on the fixed
   replays, then P5 tuning of the hot loops from the profile.
4. Decide on view distance beyond 16 blocks, and on Glide or a real-board
   `sst1Init`.
