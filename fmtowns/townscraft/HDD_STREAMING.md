# Temporary HDD terrain streaming

The current game uses the dedicated launcher-prepared SCSI ID 0 disk as a
temporary terrain archive. This is not a permanent world-save feature.
Terrain generation, world dimensions, controls, graphics settings, mob rules,
lighting and mesh algorithms are unchanged. Starting a new world replaces the
runtime archive; quitting does not create a loadable HDD save.

## Storage and ownership

The existing generator cooks the finite world once. Compressed columns are
written into sector-aligned fixed slots on the HDD. Each column has two
12 KiB slots, so a failed/partial write cannot overwrite its previous good
record. The RAM directory changes only after successful command completion.
Checksums and block validation precede releasing an old resident owner.
Edits made during a write have independent revisions and must reach a newer
record before the resident column can be released.

The current 256-entry directory reserves 6 MiB plus a 512-byte ownership header
inside the chosen 200,000,000-byte HDD. The remainder is not written. Records
retain the existing RLE/raw encoding; only their actual padded lengths are
transferred, not an entire 12 KiB slot on every load.

The launcher creates missing images and claims existing images only if the
whole file is zero or its exact header already identifies our scratch format:
16 ASCII bytes `TSC-TERRAIN-TEMP`, followed by little-endian words 1 (version),
256 (columns), and 24 (sectors per slot), with the rest of sector zero blank.
Unknown data, even with an empty boot sector, is refused without modification.
Never use an OS/save disk as runtime scratch. Each ISO has its own ignored
`runtime_1989/` image; these writable disks are not committed to Git.

If the guest cannot find a sufficiently large, correctly marked 512-byte-sector
disk, it retains the existing compressed-RAM backing. Once terrain lives on an
active HDD, failures retain resident ownership/dirty edits rather than silently
falling back to an archive that might not hold the world.

## Runtime scheduling

The freestanding driver probes only SCSI ID 0 / LUN 0. Read/write payloads use
DMA channel 1; Tsugaru's SCSI implementation has no PIO payload path. Command,
status and message bytes use ports C30/C32. A traversal poll drains at most 32
immediately progressing steps and copies at most 2 KiB, then yields on a hardware
wait. Startup cooking and explicit floppy save/load may synchronously finish I/O.

Other DMA channels and their register selector are preserved. Pending floppy
reads and HDD commands are serialized because the existing floppy driver masks
all DMA channels. Explicit saves/loads finish outstanding terrain I/O before
their first floppy transfer. Ready hardware is consumed before a timeout is
applied; a long rendering frame is not itself a failed disk command.

## Extra RAM and coprocessor audit

- The 8 MB profile adds a 32-record, 384 KiB LRU read cache. It eliminates repeated
  SCSI commands on backtracking without enlarging the 25-column lighting/mesh
  working set. Its permanent allocation happens after first-generation scratch
  is released. Writes use a separate 12 KiB buffer; DMA also has an aligned bounce
  buffer. The existing 1 MiB compressed archive remains reserved for fallback and
  legacy floppy export. Smaller profiles do not allocate the 384 KiB cache.
- The 80387 remains enabled in the launcher but unused by game math. Projection
  already uses reciprocal tables and integer multiply/shift; trig is table-based.
  Merely enabling x87 compilation would not accelerate these integer operations.
  A floating-point conversion needs its own initialization, rounding-equivalence
  tests and measured benefit; none was introduced here.
- A bigger mesh pool could use another 62.5 KiB for 8,000 quads, but this audit has
  not established that pool eviction limits this target. More resident terrain
  also changes the loading/lighting policy. Neither was changed speculatively.
- HDD DMA, compact transfers, sector alignment and RAM read caching are the
  implemented hardware opportunities. Renderer/UI changes are outside this pass.

## Validation

17 native configurations pass, including the existing terrain/light/mesh/edit,
mob, save, keyboard and rendering regressions; 8 MB cached edits; a mocked SCSI
controller; delayed HDD world I/O; and actual floppy-driver tests with mixed
HDD/floppy backing. Coverage includes absent/unknown disks, capacity and DMA
bounds, cancellation, short transfers, stopped clocks, late polls, interrupted
writes, in-flight edits, camera reversals including RAM hits, source-bank
protection, failed save entry, import and repeated generation.

The uninstrumented 8 MB production image booted in Tsugaru with 256 generated
column writes, 25 starting-column reads and zero errors. A separate test-only
real-emulator fixture edited glass/torch blocks, evicted their column, and
revisited it with exact blocks and torch light preserved: 34 reads, 257 writes,
4 RAM hits, zero errors. The host independently decoded/checksummed the physical
HDD record and verified those edits, so a RAM hit cannot mask a bad disk write.
The same production image also reached advancing 96-wide gameplay at 2 MB with
no HDD. These fixtures use dedicated disposable HDD images.

A full standard-level run at 2F-generation / 16 MHz / 8 MB / 80387, normal SCSI,
160x100 rendering and an 8-block view used the same version-1 terrain hash as
the previous RAM-backed source (`2a04c151`). It recorded 33 HDD reads (including
25 startup reads), 256 cooking writes, 57 RAM hits and zero errors. The earlier
one-error run was rejected; its late-poll timeout is now covered by regression.
These are single runs, not a general FPS-speedup claim. Disk latency changes
the timing of mesh readiness, so slightly different average visible face counts
also limit direct FPS comparisons. No HDD-versus-floppy performance factor has
been measured here.

| Standard phase | Previous RAM-backed FPS | HDD + RAM cache FPS |
| --- | ---: | ---: |
| Looking | 24.59 | 24.51 |
| Walking | 24.42 | 25.11 |
| Walking, looking down | 19.01 | 19.65 |
| Walking, looking up | 34.00 | 35.52 |
| Breaking/restoring blocks | 9.51 | 9.49 |
| Mixed movement/edits | 24.24 | 24.12 |

Reproduce from `fmtowns/townscraft`:

```powershell
python tools/run_regressions.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/launcher_disk_test.ps1
python tools/perf_audit.py --name hdd-production --ref WORKTREE --production --build-only
# Prepare a separate scratch disk with run_1989.ps1 -PrepareDiskOnly first.
python tools/smoke_production.py build/perf-audit/hdd-production --ram 8 --hdd <scratch.h0>
python tools/perf_audit.py --name hdd-standard --ref WORKTREE --standard --towns1989 --width 256 --hdd <scratch.h0> --timeout 600
python tools/perf_audit.py --name hdd-runtime --ref WORKTREE --hdd-probe --towns1989 --hdd <scratch.h0> --timeout 600
```

Do not run simultaneous emulator instances against one writable HDD image.

## Limits deliberately retained

The world is still 96/160/208/256 wide according to RAM. The generator still
needs temporary whole-world RAM, coordinates/save fields and directory sizes
are still finite, and the HDD directory is not a recoverable on-disk save.
Supporting 1024/2048 worlds still requires the separate generator/directory/
coordinate work; adding a disk does not accomplish that automatically.

PF9 and title-screen load retain the existing floppy formats. Version-4 saved
worlds can still page clean records from their floppy; dirty evictions may use
HDD scratch. The 256-wide legacy export still needs its compressed terrain to
fit the bounded RAM archive and its full-disk floppy save. HDD capacity does not
remove those explicit-save limits. Player structures survive runtime eviction,
but do not gain permanent HDD persistence in this pass.

Tsugaru's normal SCSI timing is not a calibrated simulation of a specific 1989
drive's seek/cache/transfer behavior. Real Towns hardware has not been tested.
