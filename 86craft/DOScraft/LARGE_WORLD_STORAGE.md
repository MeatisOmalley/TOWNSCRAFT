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

## Optional distant surface appendix (B8)

The distant terrain feature adds a checked two-by-two surface map after the
lighting appendix. The terrain file is now 178,815,488 bytes. The 524,288-byte
map stores the tallest cube and its block ID per cell; model plants and torches
are excluded. Base/edit offsets and seed/save identity remain fixed. Native
migration checks preserve every base/edit byte, save and other root file.
Edits update summaries in memory. Loading reconstructs summaries from the
committed edited-column directory before rendering; no frame-time disk reads
are added. [Settings, rendering limitations and costs](DISTANT_TERRAIN.md).

The B8 detail revision reuses spare high material bits for slope and stable
variation in RAM after validating the raw surface-map checksum. Disk map
bytes, base/edit records, save offsets and the fixture stay unchanged. No
extra map allocation or rendering-time HDD reads are required.

## V3 edit log (2026-10-14)

V2's fixed pool gave only 384 columns (9%) a pair of 12 KiB edit slots. V3
(`tools/convert_large_v3.py`, magic `DOSCRAFT-1024-V3`) keeps V2's immutable
bytes and replaces the pool with a preallocated edit log, so space follows the
edits actually made. Each edited column is one sector-aligned record in the
column codec (RLE, raw 12 KiB when that is not smaller) anywhere in the log.
The game still writes sectors in place; the file never grows. The DOS write
guard allows one record of up to 24 sectors inside the log extent only.

On a V3 medium each V5 directory entry holds the record's byte offset (0 for
the base record), its length and checksum, and the record the column had in
the previous committed save. Loading the newest bank therefore knows both
banks' records. The runtime keeps a sector bitmap that is never smaller than
the truth: the live directory, both committed banks and any write in flight.
Next-fit allocation marks it; an exact rebuild after a commit or load (or
before giving up) releases records nothing references. A record either bank
references is never overwritten, so a crash between a record write and the
bank commit, or a damaged newer bank, still loads the older state intact.

Play-time records must leave room for a save to flush every resident column
(26 free 24-sector runs); otherwise the column stays resident and the game
shows "Terrain backing store full" until the player saves. Saves may use
that reserve. When fewer than 52 such runs remain, a save compacts: it copies
the live records out of the 1/16 window holding the least live data into
holes elsewhere. The originals stay pinned until the next save commits.

486 media use a 24 MiB log (49,152 sectors); the 386's 40 MB disk takes what
the volume holds beside `WORLD.SAV` and 1 MiB of headroom (30,668 sectors for
this seed). Every base column edited once needs 28,343 sectors. Player disks
migrate with `tools/log_player_vm.py`, which stages a clone and changes no
launcher unless asked. Sizes, pause measurements and limits are in
[the research note](research/edit-log-v3-2026-10-14.md).

## Per-block edits (2026-10-15)

Most edits no longer reach the edit log. While a column's record is still
its base, its changed cells stay in RAM as (cell, block) pairs: evicting the
column writes nothing, and reloading it decodes the base and applies them.
Saves store them in `WORLD.SAV` after the column directory (save version 7;
the format is in `tools/convert_large_v3.py`), so the two-bank commit covers
them and a crash loses only unsaved work, as before. A column with more than
512 changed cells, or one edited while the 4,096-entry pool is full, switches
to a full record in the log and stays a log column. The pool has the same
size on every RAM tier, so any save loads on any machine.

New media therefore get a 4 MiB log (8,192 sectors) instead of 24 MiB:
`TERRAIN.TMP` drops from 49.6 MB to 28.6 MB on 486 media, and the 386's new
media take 4 MiB rather than the 15 MiB that fitted its disk. Existing logs
keep their size; V2 player migrations still use 24 MiB, since a full
384-pair pool can need 9,216 sectors. The game reads version 5 and 6 saves
and writes 6. Guest results are in
[the research note](research/per-block-edits-2026-10-15.md).
