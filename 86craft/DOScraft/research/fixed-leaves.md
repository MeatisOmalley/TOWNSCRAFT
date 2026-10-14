# Prebaked leaf exclusion with original cache updates

This revision follows the user's clarification that players can destroy leaves
but cannot place them. The dynamic trunk inference in `a28d7982` is removed.
The active code no longer gathers root heights, refreshes neighboring samples
after trunk edits, or reads adjacent columns for leaf eligibility.

The offline baker marks outer-rim leaves using an unused metadata bit in their
existing voxel byte: `B_LEAVES | 64`. Included leaves keep their ordinary value.
The selection is the trunk column plus its eight horizontal neighbors, using
the existing real leaf heights. Marking happens once when rebaking the seed.
It does not delete, add or move any logical voxel. The immutable column index
checksums are updated, and lighting data stays byte-identical.

Near-world block flags, textures, face keys and lighting use the masked block
ID, so this metadata does not alter near foliage or introduce new mesh splits.
The distant sampler alone ignores the marked leaf value. Its cache-update path
is the original highest-cube scan over the affected 2x2 cell, with one cheap
byte exclusion check. The resident boundary sampler uses the same rule inline.
All cached height/material bytes and existing shading bits keep their original
format. There is no per-frame canopy pass, height decoding, root search, or
extra map. The 512 KiB shared map retains 20,949 leaf cells versus 37,273 in the
original fixture; omitted leaf samples contain the terrain underneath.

Destroying an excluded rim leaf cannot add it back to the far surface. Destroying
an included leaf lets the original updater select the surviving included leaves
or other cubes. Removing a trunk does not hide surviving real leaves. Covering
and uncovering a canopy with a placed solid preserves the baked selection.
The original 2x2 sampling still coarsens the silhouette; this does not introduce
a second representation of tree geometry.

| Section | Original release | Fixed exclusion |
| --- | ---: | ---: |
| Text | 360,472 B | 360,472 B |
| Data | 20,992 B | 20,992 B |
| BSS | 1,053,184 B | 1,053,184 B |
| Shared surface map | 524,288 B | 524,288 B |

Executable sections, heap allocation and stack reservation are unchanged.
The voxel and HDD record sizes are unchanged; no eligibility array is allocated.
There is a small byte check when considering leaf candidates. Rendering the
newly exposed ground can also change draw cost independently of edit upkeep.

The new appendix marker is `LAF2` at offset 92. The numeric seed, terrain layout
and world dimensions remain the same, but the immutable index checksum changes
because the voxel metadata changed. `leaf_flags_media.py` migrates both checked
V5 save banks and their referenced raw/compressed edit slots. It verifies every
base column is logically identical, changes only the leaf exclusion bit in
edited leaf voxels, updates checksums/encoding fields, and preserves player,
inventory, chest, mob, seed, bank-generation and terrain-slot state. Different
seeds, corrupted records and conflicting saved-slot snapshots are rejected.
The actual closed primary player's save passes this migration validation.

Five focused far-field tests and three fixed-leaf tests pass. The real native
world verifies rim destruction, cover removal, surviving leaves after trunk
destruction, committed save/reload and selection of remaining eligible cubes.
Original renderer hashes, planar/chunky paths and source-overlay replay pass.
The exact release passes new/reload V5 guest smoke on the June 1990 DX40/8 MiB
VM, without host keyboard injection. The older three compact-baker checks also
remain passing. No 1992 VM is used.

The separate preview is `build/checks/fixed-leaves/Play-Fixed-Leaves.cmd`.
It copies and migrates the current player's logical save into its own HDD.
The primary VM, disk, launcher and shared ISO remain unchanged.

Matched PIT replay uses the same executable with original or flagged seed
data. Main graphics stays Low; distant Low/Medium/High run in one world each.
Walking is 256 measured frames at fixed diagonal yaw and zero pitch. Other
scenes retain their 32-pose fixtures. These are deterministic graphics/world-
update replay times, not full simulation FPS.

| Distant | Original standing | Trimmed standing | Original walking | Trimmed walking | Walking delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| Low | 27.471 ms | 27.440 ms | 28.627 ms | 28.724 ms | +0.097 ms |
| Medium | 35.236 ms | 35.223 ms | 39.733 ms | 40.279 ms | +0.546 ms |
| High | 46.904 ms | 46.697 ms | 59.069 ms | 60.424 ms | +1.355 ms |

| Distant | Original edit call | Trimmed edit call |
| --- | ---: | ---: |
| Low | 11.888 ms | 11.886 ms |
| Medium | 0.496 ms | 0.491 ms |
| High | 11.770 ms | 11.768 ms |

Edit upkeep is effectively unchanged, with differences below 0.005 ms in this
fixture. The smaller footprint does not eliminate the cost of rendering ground
that the original leaf silhouette hid. Walking cost reflects the changed
coverage and sampling; the added trunk-maintenance policy is absent.

[Exact media/build hashes, migration proof, smoke and replay measurements](fixed-leaves-evidence.json).
