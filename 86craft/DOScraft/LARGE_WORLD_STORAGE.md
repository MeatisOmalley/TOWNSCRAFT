# 1024-block HDD world: implementation contract

Current release: seed 4242 is prebaked with surface terrain, caves, entrances,
trees, plants and ores. The June 1990 player ISO boots a separate 206-MiB SCSI
HDD. The previous 256-world VM and ISO backup remain intact. The private guest
smoke passed new-game/save/reload, corner-to-corner streaming, dirty-column
eviction, return to spawn and V5 terrain-edit persistence without host input.
All 4,096 base-column checksums and the four corner records were verified on
the baked medium. Other seeds and in-game world generation remain deferred.
Both current 1990 and 1992 profiles now have separate prebaked-world HDDs.
The DOS terrain transport validates the contiguous FAT16 terrain extent once
and then uses absolute volume transfers to avoid backward FAT-chain scans.
Each poll still transfers at most 2 KB synchronously. The measured 0.58-1.15 s
file-seek stalls fell to about 1.4 ms per column in the private disk probe;
see the large-world hitch section in [PERFORMANCE.md](PERFORMANCE.md).

Target the June 1990, 8 MiB VM. Keep the current 256-block save playable and
unchanged. Build and test the large-world path on a private VM and disk before
making it a player option.

## Current memory boundary

Play already keeps 25 16×16×48 columns resident. Blocks and lighting occupy
600 KiB together. A further 1 MiB high-arena allocation holds compressed
off-screen columns. The 8 MiB startup reserves 672 KiB of low arena and
4,864 KiB of high arena in one `malloc`. New-world generation and legacy import
temporarily allocate all 256×256×48 blocks and one 256×256 height map:
3,211,264 bytes. Those bytes are released after capturing the columns, but
the high arena remains reserved. The low arena serves meshes, render scratch,
the shaded texture atlas, light queues, and save scratch; overflow falls back
to the high arena. Three RAM framebuffers and VGA scratch are static allocations
outside these arenas.

For a 1024×1024 world, whole-world staging would be 50,331,648 block bytes
plus a 1,048,576-byte height map, so it must be eliminated rather than resized.
Measure low/high
arena peak use during generation, load, walking, editing, and saving before
reducing either reservation. After the new-world path stops materializing the
full map, right-size the high arena for 25 resident columns, scratch, and an
explicit safety margin. Do not shrink it based only on post-generation use.

## Disk-backed world

Use a host-baked, versioned 1024×1024 world image on the writable SCSI HDD
for the first implementation. In-game generation of new seeds is deferred.
There are 64×64 = 4,096 columns; each raw column
is 12,288 bytes. Bake a deterministic seed and a per-column index, checksum,
and encoded payload. Index offsets must use 32-bit file positions. The guest
reads only the columns needed by its 5×5 resident window, prefetches ahead of
movement, and writes dirty columns through a bounded queue. Preserve two-slot
write publication or an equivalent recoverable commit protocol.

Replace the fixed 256-entry column records/map and HDD version arrays with
4,096-entry storage sized and allocated deliberately. Replace the current
256-column `TERRAIN.TMP` geometry and 20 MiB VM disk with a private disk large
enough for 4,096 columns, edits, and saves; the modeled ST4385N has capacity
for this. Keep saves separate from temporary backing. The current fixed-size
`WORLD.SAV` is approximately 1.2 MiB and its version-4 column path is only
selected below width 256, so it cannot be used unchanged for this world.
Introduce a large-world save format containing seed/image identity, player and
entity state, and a compact directory of changed-column records. The 4,096
record descriptors fit in the existing two-bank save medium; copying every
column into `WORLD.SAV` does not. Keep the prebaked base immutable on the HDD.
Reserve two additional slots per column for edits so the most recently saved
record can stay pinned until a newer save commits. On load, use the prebaked
base plus the saved edit directory. Validate checksums before publishing cache
slots. The HDD copy remains the live world; the CD, if used later, is only the
installation source.

Widen the renderer's packed x/z fields from eight to ten bits while retaining
its y, direction, kind, and occlusion fields in 32 bits. Widen chest x/z
coordinates and serialize them in the large-world save format; continue reading
the legacy byte coordinates in old saves. Expand `regionVisited` to 4,096
entries. The current generation lattice and whole-world generation path are
limited to width 256 and must never run for a 1024 world. The host prebaker
needs to emit valid initial raw columns, a checksum table, and a spawn point.

The 1024 world must not silently fall back to RAM-only compressed storage if
the prepared disk or base image is missing. Preserve the existing 256-world
path and saves as a separate compatibility mode.

## Media choice

Use the HDD for live streaming and for the initial test bake. A CD-ROM can
later distribute an immutable prebaked world, copied to HDD on installation;
edits still require HDD storage. Direct CD reads would make random column
misses much slower and cannot hold the writeback overlay.

## Verification gates

Test world-edge coordinates and index arithmetic; cold and backtracking column
loads; dirty eviction and failed write recovery; seam meshes and lighting after
arrival; edit persistence across restart; checksum rejection; memory peaks;
walking-frame stalls; and the unchanged 256-world save/load path. Only then
publish a player ISO and VM. Do not reuse the current writable player HDD as a
formatting or migration test.

## Shipped lighting appendix (2026-10-13)

The same 1024 seed now has a checked skylight appendix: `TERRAIN.TMP` grows
from 151,011,840 to 178,291,200 bytes. Base columns, the two edit slots per
column and V5 save offsets/identity stay fixed. The host bake uses the pinned
lighting opacity and flood-fill code to produce full-world skylight; each
record stores 256 height bytes and 6,144 packed skylight bytes in 13 sectors.
The added index/header fields are checked before any record is restored.

Guest lighting keeps 32 pristine columns in a 401,408-byte RAM cache at
8 MB or more, plus 6,656 bytes of read scratch and fixed metadata. Edits mark
the affected column and its eight neighbors dirty. A bake/cache restore is
allowed only if the arriving column and its eight neighbors have no dirty
flags. This conservatively excludes a five-column-wide region around an edit.
Edited neighborhoods use the original sky initialization, seeding and flood
fill. Computed edited light is no longer captured or restored: its modified
seam pass increased edit-cycle average and p95 costs. Block edits remain
durable in the existing two-slot/save protocol. Clean terrain retains baked
skylight reuse and the RAM cache for backtracking.

The publisher and 1992 refresh accept a compatible lighting upgrade only
when the seed header and original checksum index match. They copy all prior
base/edit payloads and `WORLD.SAV` into the new private FAT volume, then use
the new appendix. Unknown disk subdirectories or a different seed are refused.
Old VMs/disks remain intact; no player disk is formatted in place. Both machine
profiles boot the same shared executable and retain their own disk and CMOS.
