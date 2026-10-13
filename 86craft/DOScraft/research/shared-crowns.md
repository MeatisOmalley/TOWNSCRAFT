# Crowns on the shared log map

Visual experiment, pending player review. The shared ISO and its primary
launcher remain on the previous texture/chicken release. The separate preview
is `build/checks/capped-trees/Play-Capped-Trees.cmd`; the previous sparse-tree
preview remains intact.

The first packed-map experiment produced a narrow green cap over each trunk.
This revision builds a stepped crown: approximately three blocks across the
upper part, five across the lower part. Medium/High use the existing restrained
leaf pattern and side shading; Low uses solid colors. Near voxel leaves are
unchanged.

The shared 512x512 surface map remains 524,288 bytes. Each entry still has one
height byte and one material byte. Two formerly unused height bits encode
one to three layers of crown above a natural trunk. Neither distant path stores
leaf cells in the shared map, a separate ground map, or permanent tree records. The baker writes
`CAP1` at header offset 92; terrain, lighting and save identity stay unchanged.
The rebuilt fixture remains 178,815,488 bytes. All 6,236 log cells in this
fixture have crown markers.

The live heightmap searches nearby shared-map entries for marked roots,
projects up to 128 visible crowns in depth order, and draws two stacked pieces.
Its temporary eight-byte projections use 1,024 bytes of the already reserved
CRT stack. Terrain occlusion reuses the existing four-byte band runs: their
fog byte also packs six bits of sample depth. Moving frames therefore capture
these runs too. There is no new depth buffer or heap allocation.

During panorama rebuilding, neighboring root markers expand into a five-block
footprint. Cached panorama samples retain their existing four-byte format and
reproject at the current eye height. This adds map reads at rebuild time, but
does not require a second persistent map.

This is an approximation: roots use their two-block summary cell's center,
crowns have a common stepped shape, and the panorama's expanded footprint is
still an extruded heightmap silhouette. Cutting the marked trunk or removing
its leaves updates the shared summary. New player-placed logs use an existing
metadata bit so they do not acquire synthetic crowns. An older unmarked log
stack with the same natural-trunk/leaf shape can still qualify.

| Release | Text | Data | BSS | Additional surface heap |
| --- | ---: | ---: | ---: | ---: |
| Shared log baseline | 360,984 B | 20,992 B | 1,053,184 B | 0 B |
| Wide packed crowns | 363,032 B | 20,992 B | 1,053,184 B | 0 B |

Code grows by 2,048 bytes; data and BSS stay unchanged. The stack reservation
is unchanged. This preserves the data-RAM savings of the log-map checkpoint,
but crown rendering and panorama expansion still cost CPU time.

Seven focused native checks pass. They cover seed/extent preservation, packing,
real streamed edit/save/reload behavior, placed-log exclusion, wider lower
crowns, full/partial terrain occlusion, chunky/planar targets, stationary cache
reuse, panorama reprojection and unchanged cache record sizes. The exact
release executable passes new/reload V5 smoke on the June 1990 DX40/8 MiB VM,
using guest-driven input. No 1992 VM or host keyboard injection is used.

The matched replay holds main graphics at Low and sweeps distant Low/Medium/High
in one prebaked world. Walking advances continuously for 256 measured frames,
with fixed diagonal yaw and zero pitch. Other scenes retain their 32-pose
fixtures. Both builds use the same packed seed; the control masks crown bits
and draws the shared log map. Crown time is included in the heightmap timer.
Frame-dump diagnostics are separate from the timing comparison.

| Distant level | Log-only standing | Crowns standing | Log-only walking | Crowns walking | Walking delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| Low | 27.40 ms | 28.70 ms | 28.78 ms | 31.23 ms | +2.45 ms |
| Medium | 35.83 ms | 37.12 ms | 41.81 ms | 45.95 ms | +4.14 ms |
| High | 48.33 ms | 51.33 ms | 63.82 ms | 72.79 ms | +8.97 ms |

These are whole-frame means from the deterministic guest replay, not full
simulation FPS. Low distant work alone rises from 6.43 to 8.29 ms walking,
and from 3.91 to 4.97 ms standing. Low walking components:

| Component | Log-only | Crowns |
| --- | ---: | ---: |
| Panorama rebuild | 0.095 ms | 0.457 ms |
| Panorama draw | 1.520 ms | 1.657 ms |
| Heightmap including crown draw | 4.814 ms | 6.173 ms |

This revision preserves RAM but is not a speed improvement. Crown filling,
moving-frame strip capture, sorting and footprint expansion add work. The
whole-frame Low delta also includes changed VGA transport costs. Comparison
with the earlier sparse-tree experiment does not isolate a leaf pass because
the surface representation and panorama coverage differ.

[Build hashes, smoke output and matched replay measurements](shared-crowns-evidence.json).

The preview preserves the primary `WORLD.SAV` and all terrain/edit slot bytes.
The primary disk hash remains
`3a588a6614c9991ff6869764a33b5352da7282a1139e363ade8beab1c60db0de`.
The shared ISO hash remains
`a5e69ab2405a665bc8ffc2e67987d718904fbe3573f587f6ce5132fd58b75db0`.

The 64-frame diagnostic animation is
`build/checks/capped-trees/wide-diagonal-walk.gif`: every fourth frame of the
High distant-terrain walking route, displayed at a fixed 150 ms per capture.
Its playback speed is illustrative, not an FPS measurement.
