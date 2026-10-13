# Tree canopy experiment checkpoint

The first implementation was checkpointed before testing. The revised version
now passes native storage/render checks and June 1990 8 MiB new/load smoke.
It remains a visual experiment: the shared ISO still contains the tested
texture/chicken release documented in `CHICKENS.md`.

The experiment stores sparse leaf-free surface exceptions and natural-tree
records alongside the existing far field. It draws compact crowns over the
log-height terrain band, while retaining the original panorama data. Planted
logs are marked separately so they do not acquire automatic crowns.

The appendix adds 208,896 bytes for 37,273 sparse ground samples and 6,533
natural trees. Ground occlusion uses a 10,240-byte skyline transition list,
rather than the first version's per-row depth buffer. Save/load preserves
player-log provenance, crown removal, original terrain edits and V5 saves.
The original panorama map is byte-identical; its drawing can cost more because
the shorter ground skyline leaves more background visible.

An early build accidentally fell back to ordinary DOS file I/O because the
contiguous-extent eligibility check recognized only the old fixed file sizes.
The revised check validates the tree appendix dimensions and keeps the existing
FAT-chain and identity checks before enabling absolute I/O. Direct-write bounds
remain restricted to terrain edit slots.

One 256-frame continuous diagonal walk at fixed horizon-facing yaw was run at
each distant level, holding main graphics at Low on the 40 MHz/8 MiB player
hardware. The control uses the same instrumented executable without the tree
appendix. Other scenes use the existing 32-pose fixtures. These are guest PIT
graphics/world-update replay timings, not full gameplay FPS.

| Distant setting | Original mean frame | Tree mean frame | Added time |
| --- | ---: | ---: | ---: |
| Low | 28.86 ms | 29.99 ms | 1.13 ms |
| Medium | 40.35 ms | 44.42 ms | 4.07 ms |
| High | 60.58 ms | 72.00 ms | 11.42 ms |

World-update averages return to the control's 5.2–5.7 ms after the extent fix.
The remaining increase is drawing: removing leaf walls exposes more ground
strips/background, and compact crowns add another pass. This is **not a
performance improvement on the measured forest route**. Visual approval and
any decision to retain it are pending. [Evidence](tree-canopy-evidence.json).

The prepared trial is `build/checks/tree-canopies/Play-Tree-Preview.cmd`.
It has its own copied save/HDD and ISO; enable the existing Distant terrain
setting to inspect it. It does not publish over the shared player ISO or
change the 1992 VM. Reproduce the preview after an exact-build smoke with:

```powershell
python tools/tree_canopy_preview.py --terrain build/checks/large-world-proto/terrain1024-trees.bin --build-output build/checks/tree-canopies/transition-release --smoke-vm runtime/game-smoke-psz36und
```
