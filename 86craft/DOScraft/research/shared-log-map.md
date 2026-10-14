# Shared leaf-free distant terrain

This is a tested checkpoint, pending the user's choice of appearance. The
shared ISO was briefly published before the user's hold request arrived,
then restored to the previous texture/chicken release. Both player VMs and
the log-only build remain intact; their saved world/edit bytes were checked
identical before restoring the previous launcher pointer.

The user chose one map for both distant render paths to avoid the canopy
experiment's RAM cost. The shared 512x512 height/material cache remains
524,288 bytes. Highest-cube selection now skips leaves during baking, edit
updates, committed-save reconstruction and resident boundary sampling.
Logs remain visible. Near voxel terrain and its leaf blocks are unchanged.

Canopy compositing, natural-tree/provenance records, sparse ground exceptions,
the second cache epoch and canopy occlusion buffers are removed. The old
trial is tagged `codex/tree-canopies-experiment-2026-10-13`. Its offline
baker/preview tools and evidence remain historical artifacts, outside the
active renderer. New planted logs are ordinary logs again.

Executable section sizes measured with DJGPP size:

| Build | Text | Data | BSS | Extra surface heap |
| --- | ---: | ---: | ---: | ---: |
| Canopy trial | 366,104 B | 20,992 B | 1,070,080 B | 208,896 B |
| Shared log map | 360,984 B | 20,992 B | 1,053,184 B | 0 B |

The change saves 225,792 bytes (220.5 KiB) of extra data RAM, plus 5,120 bytes
of text. BSS is back to the pre-experiment chicken release's size. Neither
heightmap nor panorama maintains a second surface cache.

Rebaking an existing lit/distant seed writes the new map into the same file
extent, strips the experimental appendix and preserves terrain/lighting
offsets and V5 save identity. Publication migrates all old base/edit slot
bytes and the save into the new disk. Older media with leaf-inclusive samples
is repaired during load using existing column scratch; a rebaked fixture
avoids those compatibility reads. The fast validated contiguous-file disk
transport remains eligible.

Five focused native tests pass: baker selection and seed/offset preservation,
source-overlay replay, the real streamed world with both old/new map files,
log/leaf edits and save/reload, and real renderer/raster paths with panorama
and resident samples excluding leaves. Both June 1990 8 MiB guest smoke
phases pass on the exact release executable. No host key input was used.

The walking replay holds main graphics at Low, advances diagonally for 256
measured frames at fixed 45-degree yaw and zero pitch, and evaluates distant
Low/Medium/High in one world. Other scenes retain their 32-pose fixtures.

| Distant | Original total frame | Canopy total frame | Shared log total frame |
| --- | ---: | ---: | ---: |
| Low | 28.86 ms | 29.99 ms | 28.77 ms |
| Medium | 40.35 ms | 44.42 ms | 41.82 ms |
| High | 60.58 ms | 72.00 ms | 63.85 ms |

Low distant drawing alone:

| Component | Standing | Walking |
| --- | ---: | ---: |
| Panorama rebuild | 0.11 ms | 0.10 ms |
| Panorama draw | 1.39 ms | 1.51 ms |
| Heightmap | 2.41 ms | 4.82 ms |
| Total | 3.90 ms | 6.43 ms |

The old canopy total is 4.18 ms standing and 7.63 ms walking. Comparing these
variants does not isolate the leaf compositing pass: removing leaves also
changes visible ground and panorama coverage. The shared log version is
approximately even with original Low in this route; Medium/High still cost
more than original, despite being cheaper than the canopy experiment.

[Raw replay, smoke and publication evidence](shared-log-map-evidence.json).
