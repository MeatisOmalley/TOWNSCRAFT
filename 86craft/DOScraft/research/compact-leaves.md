# Original distant renderer with compact leaf coverage

Superseded by the destroy-only leaf policy: [prebaked leaf exclusion](fixed-leaves.md).
This document records the dynamic inference checkpoint at `a28d7982`.

This trial follows the user's request to use the original system and retain
the trunk column plus its eight horizontal neighbors. Both the heightmap and
panorama consume one shared surface map again. The separate crown projection,
sorting, drawing, occlusion bookkeeping, height-bit packing and player-log
marking experiment is removed from the active source. Its complete checkpoint
remains at commit `7967067b`, with its own prepared preview and evidence.

The baker keeps real leaf voxels within one block in X/Z of a real log stack,
at the original canopy heights. It invents no leaves. The resulting footprint
is up to 3x3 voxel columns per trunk; actual empty/cut leaf cells stay empty.
Tree crowns are no longer widened during panorama sampling. Since the original
surface map groups 2x2 voxel columns, a 3x3 footprint can occupy four summary
cells and appear roughly four blocks wide. It is not a new nine-voxel tree
record or a second cache.

The same 512x512 height/material map remains 524,288 bytes. This fixture has
20,949 leaf summary cells, versus 37,273 originally: 43.8% fewer. The map still
stores one top height/material per cell, so narrower coverage does not reduce
its RAM allocation. The original heightmap's solid leaf-side approximation
remains; it is narrower, rather than separately composited over ground/logs.

The optional media marker `DOS_FAR_COMPACT_MAGIC` at header offset 92 selects
the clipping rules. Base terrain, lighting, edit slots, checksums identifying
the seed and V5 saves stay unchanged. File size remains 178,815,488 bytes.
Ordinary old media follows the original full-leaf behavior.

Edits recompute the affected 3x3 area, including neighboring chunk edges.
Top log heights use a temporary 324-byte stack halo, obtained from supplied or
resident blocks. During save reconstruction only, missing neighbors are read
from checked raw/compressed records through the existing column scratch.
Normal streaming never synchronously reads a missing neighbor for this work;
unknown edges retain their previously baked/edited samples. A cheap highest-
cube check skips root gathering when no leaf can become the chosen surface.

The resident heightmap boundary consults the same clipped map when its detailed
voxel stack ends in leaves. It does not search neighboring trunks per ray.
Near polygon blocks, leaf transparency, gameplay and lighting are unchanged.
Player-placed logs alone cannot create crowns because only existing leaves
are considered. Real placed leaves near real logs can qualify.

| Release | Text | Data | BSS | Shared map | Additional heap |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original texture/chicken release | 360,472 B | 20,992 B | 1,053,184 B | 524,288 B | 0 B |
| Compact leaves | 363,544 B | 20,992 B | 1,053,184 B | 524,288 B | 0 B |

Data/BSS and stack reservation are unchanged. Code grows by 3,072 bytes for
clipping and consistent edit/save reconstruction. No tree/ground/depth buffer
is allocated, and no separate leaf pass is added to rendering.

Eight focused native checks pass: exact 3x3 selection, border roots, preserved
seed/extents, source-overlay replay, original renderer image hashes, real
cross-chunk leaf/trunk edits, raw/compressed save reconstruction and missing-
neighbor behavior without disk preparation. The exact final release passes
June 1990 DX40/8 MiB new/reload smoke with V5 saves and guest-driven input.
No 1992 VM or host keyboard injection is used.

The timing comparison uses the same executable with either the original or
compact seed appendix. Main graphics stays Low; distant Low/Medium/High run
in one prebaked world each. The 256-frame walk has fixed diagonal yaw and zero
pitch, and the other scenes keep their 32-pose fixtures. These are guest PIT
graphics/world-update replay timings, not full simulation FPS. Frame-capture
diagnostics are separate from performance evidence.

| Distant level | Original standing | 3x3 standing | Original walking | 3x3 walking | Walking delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| Low | 27.478 ms | 27.448 ms | 28.692 ms | 28.819 ms | +0.127 ms |
| Medium | 35.254 ms | 35.247 ms | 39.850 ms | 40.366 ms | +0.516 ms |
| High | 46.943 ms | 46.747 ms | 59.301 ms | 60.544 ms | +1.243 ms |

The smaller footprint is not a rendering speedup on this forest route: revealed
ground replaces leaf-side coverage. It avoids the additional wide-crown pass
while reducing the amount of green occupying the scene. Low distant work alone
rises by 0.115 ms walking and falls by 0.035 ms standing. Medium/High walking
distant deltas are +0.558/+1.245 ms. These are measured changes to the whole
distant representation, not isolated timings for drawing nine leaves.

Ordinary block-edit call costs are 11.896 -> 12.033 ms on Low,
0.501 -> 0.642 ms on Medium, and 11.782 -> 11.917 ms on High. The much larger
edit overhead in the first draft was removed by skipping unused root gathering;
the final build costs about 0.14 ms more per ordinary fixture edit. Tree/leaf
edits that actually require root gathering follow the full clipping path.

[Exact build/media hashes, smoke output and matched replay measurements](compact-leaves-evidence.json).

The separate preview is `build/checks/compact-leaves/Play-Compact-Leaves.cmd`.
It preserves the primary save and terrain/edit slots. The shared ISO and
primary launcher remain untouched pending visual review.

The user questioned the extra edit work. This checkpoint restores the original
draw loops, but its dynamic cache-maintenance policy is additional work:
candidate leaves are checked against current trunks and neighboring summaries
are refreshed. Cutting a trunk can therefore remove distant leaf coverage even
while the actual near leaf voxels remain. That is not the original update
algorithm operating on a physically smaller cache. A fixed eligibility flag
in spare cache bits would be a different policy and could avoid the root halo;
the current preview/evidence still uses dynamic inference.

`build/checks/compact-leaves/compact-diagonal-walk.gif` contains 64 captures of
the 256-frame High distant-terrain walk. Frame durations use quantized sums of
four guest frame times from the separate measured run, not capture wall time.
