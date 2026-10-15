# Face-adjacent distant crowns and the 2 MiB investigation

The primary 1990 world retained its reduced-leaf `LAF2` medium. The separate
386 and 1992 worlds had the original full-leaf medium. The 386 preparation tool
selected `terrain1024-far.bin` when creating the new compact disk; executable
refreshes copied that disk unchanged. The old reduced world was not overwritten,
but the wrong source was selected for the new machine.

The new offline crown bake includes only existing leaves sharing one of the six
faces of the highest log in a vertical stack. A normal continuous trunk leaves
five eligible positions: four beside its top log and one above. Diagonal leaves,
another canopy layer and leaves beside lower logs are excluded from the distant
surface. Near blocks, lighting and tree gameplay remain unchanged. No runtime
trunk search or new cache array is added. The existing exclusion metadata and
height/material sampler handle the new selection.

| Seed representation | Distant leaf summary cells |
| --- | ---: |
| Original full leaves | 37,273 |
| Previous 3x3 reduced leaves | 20,949 |
| Face-adjacent crown | 16,593 |

The 2x2 summary sampling still coarsens the silhouette. Counts describe occupied
summary cells, not extra tree objects or allocated RAM. The compact V2 seed is
33,835,008 bytes; metadata can create more RLE runs even with fewer far leaves.

`bake_far_field.py --crown-leaves` writes the new seed exclusively. New 386
preparation and benchmark defaults select this seed. Old benchmark records keep
their original media hashes; no new FPS claim is made for the changed canopy.

`leaf_flags_media.upgrade_v2` checks both compressed base indexes, logical block
equivalence and unchanged lighting, retains pool pair numbers and both V5 save
banks, and updates leaf eligibility in referenced raw/compressed edits.
`crown_leaves_media.py` clones a stopped VM and migrates its disk while preserving
geometry, other files, player state and CMOS. Originals remain available. It
rechecks the source before switching an explicitly selected launcher pointer.

Focused checks cover exact face adjacency, highest-log metadata, chunk-border
neighbors, idempotence, preservation of real voxels, both save banks, raw and
compressed edited columns, moved pool offsets, corrupt-save rejection, changed
terrain rejection and disk roundtrips. The real streamed-world harness also
tests crown destruction, cover removal, trunk removal and committed save/reload.
The native harness passes with both the raw V1 and compact V2 crown media.
All 12 focused checks and all three launcher validations pass. The three stopped
player VMs now select migrated clones; their original disks remain intact and
their boot images, hardware configurations and CMOS are byte-identical.
[Migration and 2 MiB evidence](crown-leaves-and-2mb-2026-10-14.json).

## What blocks 2 MiB now

A private 386SX VM with 2048 KiB installed, matching DTK CMOS extended-memory
fields, paging disabled and the current benchmark executable exits before game
entry with **`Load error: no DPMI memory`**. Its report and private VM are retained
in `build/checks/386-2mb-current-build.json`; the completed diagnostic was closed.
An initial shell-redirection trial was inconclusive and is not evidence.

The current production executable's loaded sections are 398,360 bytes text,
20,992 bytes data and 1,085,440 bytes BSS: **1,504,792 bytes (1.435 MiB)** before
the 64 KiB stack and runtime overhead. The existing 2 MiB large-world path also
requests 768 KiB low and 768 KiB high arenas. Conventional memory backs part of
the low arena; it is not extra physical RAM beyond the installed total.

Concrete savings worth investigating:

- Three software framebuffers consume 360 KiB. One could save 240 KiB, with HUD
  and partial-redraw handling revised and verified.
- The 25 resident block/light columns consume 600 KiB. A smaller working set
  could save hundreds of KiB, but lighting, collision, mob spawning and prefetch
  would need a coherent replacement for their existing reach assumptions.
- Packing/HUD buffers, duplicate column indexes and camera-plane tables provide
  additional targets. Selectable video paths currently retain their buffers in
  BSS even when unused by a Trident machine.

This is substantial memory work, not a RAM-setting change. Two MiB is not ruled
out as a dedicated lean configuration, but current functionality/performance is
not demonstrated at that size. Shrinking leaf eligibility does not shrink the
allocated distant map/window or resident voxel buffers; RLE disk size does not
equal runtime memory use. No production memory/rendering changes were made for
this investigation.
