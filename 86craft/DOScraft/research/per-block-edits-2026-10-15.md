# Per-block edits and a 4 MiB log (2026-10-15)

The V3 edit log stored every edited column as a whole RLE record (about 3.3
KB, 7 sectors), so a single torch cost a full record and the log was sized
at 24 MiB to hold every column once. Now a column whose record is still its
base keeps its changed cells in RAM and in the save; only heavily edited
columns use log records. The contract is in
[LARGE_WORLD_STORAGE.md](../LARGE_WORLD_STORAGE.md#per-block-edits-2026-10-15).

## Design

- `world_set` records `(cell, block)` for the column (`large_delta_note`),
  replacing an earlier entry for the same cell. Cells use the column codec's
  index, so a reload decodes the base and applies the pairs
  (`column_restore_valid`); every reader of edited columns (streaming, the
  distant-terrain summaries) goes through it.
- Eviction of such a column writes nothing (`large_delta_keep`).
- Saves write version 6: the V5 directory, then each edited column's pairs.
  Load validates order, ranges and duplicates, and refuses a column that has
  both a log record and pairs. Version 5 saves still load.
- Past 512 cells in one column, or when the 4,096-entry pool is empty, the
  column drops its pairs and is written whole on its next eviction or save,
  as before. A log column stays one.
- RAM: a 20 KiB pool (low arena, same size on every tier, so any save loads
  anywhere) and 8.5 KiB of static tables. The executable grows by 4 KiB.

## Media size (seed 4242, crown-leaf bake)

| | Log | `TERRAIN.TMP` |
| --- | --- | --- |
| Before (486 media) | 49,152 sectors (24 MiB) | 49,563,648 bytes |
| Before (386 media) | 30,668 sectors (what fitted) | 40,099,840 bytes |
| New media, both | 8,192 sectors (4 MiB) | 28,592,128 bytes |

Existing player disks keep their log until `tools/log_player_vm.py` moves
them onto 4 MiB (below). V2 player migrations still create a 24 MiB log.

## Native tests (`tests/test_edit_log.py`)

The log scenarios now force full records (`DELTA_MAX=0`) so they keep
exercising the log; new scenarios use the game's limit:

- 100 columns with 3 edits each: no log record, no terrain write, the terrain
  file byte-identical; edits survive eviction in the same process and a
  fresh-process reload.
- Every column edited once: exactly fills the pool; 8 ms save; reload passes.
- Columns past the limit switch to log records and stay log columns; edits
  in other columns stay pairs.
- An exhausted pool switches the next column to a log record.
- A version 5 save still loads; corrupt pairs (cell past the column, a cell
  twice) are refused by the game and by `convert_large_v3.v3_records`.
- `tests/far_world_native.c` saves and reloads edits through pairs and checks
  that the distant-terrain summaries are rebuilt from them.

## 1990 target (486DX/40, 8 MiB, ET4000, ST4385N SCSI)

**Smoke** (`tools/large_game_smoke.py`, private VM `game-smoke-3mc_n2r6`,
executable `0ad79152...`, new 4 MiB-log media): new game and fresh-process
reload both PASS, saves are version 6. Terrain persistence, edge streaming
(corner travel evicts and reloads the edited spawn column), light cache,
distant terrain, chickens, menus and all graphics presets pass. The edits
were held as pairs (`DELTA=5,1,0` new, `8,1,0` after reload); the log was
not used, so `LOG_COPY` reports -1 (no record to time) where it reported
21 ticks before.

**Benchmark** (`tools/benchmark.py --candidate production --large-terrain
... --stream-light --far-field low --far-details --far-view-depth
--far-reproject --hardware-vm <1990 player>`, three repeats each, run back to
back on `master` and this change):

| Scene | master | per-block edits |
| --- | --- | --- |
| open_static | 28.069 ms | 28.091 ms |
| walk | 39.635 ms | 39.638 ms |
| walk_down | 41.471 ms | 41.475 ms |
| walk_up | 23.123 ms | 23.126 ms |
| house | 39.190 ms | 39.194 ms |
| block_edits | 37.827 ms | 37.833 ms |

All differences are under 0.1% and every frame image matches
(`tests/benchmarks/2026-10-15/delta-edits-1990-comparison.json`). The
replay edits without travelling, so it never evicts an edited column: the
saving there (no 7-sector record write per edited column left behind) is
real but not measured by this replay.

## Moving existing V3 players onto 4 MiB

`tools/log_player_vm.py` now also takes a V3 disk with a larger log
(`convert_large_v3.shrink_log`; contract in
[LARGE_WORLD_STORAGE.md](../LARGE_WORLD_STORAGE.md#per-block-edits-2026-10-15)).
Native tests (`tests/test_edit_log.py`):

- A 24 MiB-log disk with two banks that differ (400 log columns each, the
  older bank's records past 4 MiB) and 50 per-block columns: 800 live
  records (5,576 sectors) packed into 4 MiB in 4 to 6 s, 400 moved,
  20 MiB more free on the volume. The game loads either bank with its own
  records and edits, then keeps playing (the head wraps; nothing refused).
- 2,000 live records (about 14,000 sectors) are refused for 4 MiB and move into
  a 16,384-sector log; the older bank's 1,000 refs to the bank it replaced
  are dropped and it still loads alone.
- Refused: a newest bank that does not pin the older bank's records, a
  damaged bank, a damaged live record, a log that would not shrink.
- `install` stages a clone and refuses an executable without per-block
  edits; the original VM, its disk and the pointer are unchanged.

Dry runs (`--check`) on the three player disks: none has a committed save
or a live record, so each becomes the same 28,592,128-byte terrain (sha256
`6bb05b5e...`), 4.7 to 5.2 s each, with pointer and disk unchanged.

| Player (pointer) | VM | Log | Free on the volume |
| --- | --- | --- | --- |
| 1990 (`486dx25/latest-play-vm.txt`) | game-play-fpbku0by | 49,152 -> 8,192 | 155.4 -> 176.3 MB |
| 1992 (`486dx25/latest-1992-vm.txt`) | game-log-u3olnpvc | 49,152 -> 8,192 | 155.4 -> 176.3 MB |
| 386 (`386sx25/latest-play-vm.txt`) | game-386-r7c7pc9n | 30,668 -> 8,192 | 1.0 -> 12.6 MB |

All three boot `68a9d2a5...`, which has no per-block edits, so a real
migration needs `--boot-from` a tested release VM with this executable.
No pointer or player VM was changed.

## 386SX-25 (4 MiB, Trident, CP3044 IDE)

`prepare_386_vm.py --build --smoke`, executable `0ad79152...` (the 1990
smoke's), new 4 MiB-log media. The game smoke's 486 checks fail on the 386
as HARDWARE_386.md says (VGA_READBACK, FULL_RES_OPTION, GRAPHICS_PRESETS,
MENUS, DISTANT_TERRAIN, hence RESULT=FAIL), so AUTOEXEC's errorlevel gate
skipped the reload; it was run from the committed save afterwards. A
baseline with master's `68a9d2a5...` on the same profile fails exactly the
same six lines in both runs.

| | New game | Reload |
| --- | --- | --- |
| SAVE_RESULT / LOAD_RESULT | 0 / 0 | -1 (as on the 1990) / 0 |
| TERRAIN_PERSIST, EDGE_STREAM, LIGHT_CACHE, CHICKENS | PASS | PASS |
| DELTA (entries, columns, switched) | 3,1,0 | 4,1,0 |
| LOG_STATS refused | 0 | 0 |
| Save banks | version 6 | version 6 |

Memory (`-DDOS_MEM_REPORT -DDOS_MEM_REPORT_TIER`: peaks within the 4 MiB
tier's own arenas; plain DOS_MEM_REPORT asks DPMI for arenas a 4 MiB
machine cannot supply and stops at start-up): conventional memory holds
417,248 bytes of the 896 KiB low arena and the rest joins the 768 KiB
high arena (1,286,688 bytes). Peak use is 417,080 low and 1,195,232 high,
in the new game and the reload alike: 1,612,312 of 1,703,936 bytes, 91,624
bytes (89 KiB) spare. The pool's three allocations (8 + 8 + 4 KiB) sit in
conventional memory. Stack peak 26,940 of 64,748 bytes.

Raw reports: [per-block-edits-386-2026-10-15.json](per-block-edits-386-2026-10-15.json).

## Not run

The 1992 profile was not run.
