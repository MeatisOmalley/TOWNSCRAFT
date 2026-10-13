# Live heightmap cell-boundary DDA — rejected

Rejected on 2026-10-13: the user did not find the moving terrain visually
better, and the experiment increased render time. It was never published to
the shared ISO. The active renderer is restored to the accepted `c6bcbb07`
implementation. Do not retry this approach unchanged.

## Approach

Traverse actual world-grid boundaries instead of camera-relative depth
samples. Visit one-block cells near the polygon boundary, then two-block cells
in the distant map. Advance both axes at exact diagonal corners, project cell
entry/exit depths, and retain green grass tops with brown exposed sides.
The cached panorama was unchanged.

The revision cached directions and axis reciprocals, interpolated a finer
projection table, merged adjacent cells with matching height/appearance/fog,
and stopped at fully occluding terrain. A spurious first-cell grass lip on
flat ground was removed; grass caps on actual rises were retained.

## Measurements

June 1990 player hardware: patched 86Box, i486DX at 40 MHz, ET4000AX,
8 MB RAM and SCSI. Main graphics were Low; sweeps varied the independent
distant-terrain setting. Worlds were prebaked on private HDDs. Guest PIT
timings describe frozen graphics/world-update replays, not full simulation FPS.

The standard short replay gives this comparison for distant High:

| Scene | Accepted renderer | Revised DDA | Extra time |
| --- | ---: | ---: | ---: |
| Walking | 61.12 ms | 68.27 ms | +7.15 ms |
| Turning in the house | 65.80 ms | 83.13 ms | +17.34 ms |

See [accepted baseline](baseline-material-eye.json), [initial DDA](dda-grid-sweep.json)
and [revised DDA](dda-merged-grid-sweep.json). These are short replay samples,
not a sustained horizon-facing walk. Raw frame times, hardware hashes, flags
and source recipes are retained.

The corrected continuous diagonal walk covered 512 measured frames, about
90 blocks diagonally, with fixed 45-degree yaw and level pitch:

| Distant setting | Total mean frame time | Live heightmap mean |
| --- | ---: | ---: |
| Low | 33.40 ms | 6.59 ms |
| Medium | 46.70 ms | 18.31 ms |
| High | 71.30 ms | 42.26 ms |

See [continuous horizon run](dda-continuous-horizon.json). Its generic
`motion_policy` header still says `REPEAT_32_MEASURED_POSES`; that header was
corrected afterward. The explicit `walk_route` identifies the continuous
route, and scene sample counts are 32/512/32/32/32/32. The mistakenly launched
older 512-frame test really repeated every 32 poses and is excluded here.

A matched baseline for the continuous route was not completed before rejection.
Do not compare its timings directly against the short-route baseline or claim
a measured improvement in temporal appearance from them.

Four focused native tests passed, including cell intervals/corner traversal,
grass coverage and identical flat-plane images during fractional diagonal
motion with distance shading held constant. That isolated result does not
establish better appearance on real hills. The two-block maximum-height map,
pixel projection, material/fog transitions and incremental panorama updates
still introduce discrete changes. User review rejected the visual result.

## Recovery and retained work

`experiment.patch` preserves the full tracked working-tree experiment relative
to `c6bcbb07`, including the benchmark improvements. To reconstruct on a
separate checkout of that checkpoint, apply it from the repository root, then
copy [far_dda.h](far_dda.h) and [far_dda_march.inc](far_dda_march.inc) into
`86craft/DOScraft/src/platform/dos/`. Use `--far-dda --far-view-depth` with
the other flags recorded in the reports. Do not apply the patch blindly over
the current tree: the continuous-walk benchmark changes were retained.

Retained: optional uninterrupted diagonal horizon walking, per-scene sample
counts, short non-walking scenes and optional motion captures. Normal replay
routes and production gameplay are unchanged. Removed: the DDA build switch,
renderer hooks and active DDA-specific tests. Private VM disks/reports remain
available; no diagnostic emulator remains running.
