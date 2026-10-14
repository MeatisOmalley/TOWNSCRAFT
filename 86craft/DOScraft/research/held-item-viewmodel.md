# First-person hand and held items â€” 2026-10-14

A simple 3D forearm appears when the selected slot is empty. Solid placeable
blocks replace it with a textured cube. Every selected flat item, including
tools, food and torches, replaces the hand with a large transparent textured
plane. The plane angles the head away from the eye and the grip toward the
lower-right corner. Item/world textures and the palette are reused; the empty
arm uses clean flat skin-colored faces.
There is a small ground-walking bob and a 0.3-second action swing. Eye-position
sky/block light shades the model, with slightly darker side faces on cubes.

The foreground draws after the world/selection outline and before the HUD,
through the current raster target. It does not enter world collection, sorting,
meshing or occlusion. The existing full, planar Low, Very Low and Potato paths
handle its resolution, clipping and row proofs. No settings or gameplay rules
were changed. A8 remains deferred behind this requested graphical feature.

## Memory

The production object contains 2,960 bytes of code, 16 bytes of initialized
state and 4,208 bytes of BSS. The BSS includes one 4 KiB shaded-item page, reused
for whichever flat item is held. Cubes use the existing atlas; the empty arm
uses flat palette colors.
This page uses wrapped texture addressing because DJGPP COFF only supports
16-byte object alignment; the no-wrap atlas loop requires page alignment.
No per-item models or texture pages are allocated.

## Verification

- Native actual x86 raster/assembly: every item at all four resolutions,
  chunky and planar paths, bright/dark light, full swing poses, framebuffer
  guards, untouched HUD/padding, hidden state, animated versus resting images.
- Source overlays replay exactly against the immutable imported snapshot.
- Production flags and benchmark parser/control tests pass.
- Exact production EXE new-world and reload smoke tests pass on the June 1990
  DX/40, 8 MiB, ET4000AX ISA16, AHA-1542A/ST4385N VM. Both pass VGA readback,
  resolution/preset changes, inventory, terrain editing, streaming and V5 saves.
  Private fixture captures empty hand, cobblestone, pickaxe and torch, then
  restores the original inventory before ordinary testing/saving.
- No host key injection; smoke input is generated inside the guest.

## Performance

Matched feature-on/off replays use Low graphics, view 8, distant terrain Off,
prebaked 1024 HDD terrain, 256 frames of continuous diagonal horizon walking
and the standard 32-pose remaining scenes. Three held-item cases share a
single world per executable: empty hand, cobblestone, iron pickaxe.
The separate model timer measures projection and rasterization, including an
item-page refresh when needed. It lives inside the existing Finish section;
do not add it to that section again when totaling frame time.
The frozen replay does not include normal physics or animated swinging/bob.

Results and publication identity are recorded in `held-item-viewmodel.json`.

| Held item | Model draw, still | Model draw, walking | Total frame change, still | Total frame change, walking |
| --- | ---: | ---: | ---: | ---: |
| empty_hand | 0.570 ms | 0.568 ms | +0.583 ms | +0.889 ms |
| cobblestone | 1.045 ms | 1.046 ms | +1.066 ms | +1.223 ms |
| iron_pickaxe | 1.735 ms | 1.737 ms | +1.750 ms | +1.865 ms |

One matched pass per case does not establish small whole-frame differences
precisely. Presentation phase and dirty-row traffic can change when extra
drawing moves the flip boundary; the separate model timer is more consistent.

The revised shared ISO is published from the exact smoke-tested production
build. Its EXE SHA256 is `98a71d27349621b665417df0f13dddabcf5806a90de358808613b0c8652bb9dd`.
The current player HDD is byte-identical to the preceding player disk.
