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
- Saves write version 7: version 6 (6-bit block IDs, from master the same
  day) followed by each edited column's pairs. Load validates order, ranges
  and duplicates, and refuses a column that has both a log record and pairs.
  Version 6 saves still load; version 5 stays refused, as on master.
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

Existing player disks keep their log; shrinking them needs a migration that
moves live records, which is not part of this change. V2 player migrations
still create a 24 MiB log.

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
reload both PASS, saves were version 6 in this run (the per-block format
was renumbered 7 when merged with master's 6-bit block-ID version 6; see
below). Terrain persistence, edge streaming
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

## Not run

The 1992 and 386 profiles were not run. The 386's 4 MiB tier has about
40 KiB of low-arena headroom by earlier measurements; the 20 KiB pool should
fit but has not been measured there.

## After merging master (save version 7)

Master meanwhile made version 6 the 6-bit block-ID save and refused version
5, so per-block edits became version 7: the game writes 7 and loads 6 and 7.
The merged executable (`5bdb04e3...`) passed the same 1990 smoke on fresh
4 MiB-log media (`game-smoke-8bsfhbfw`): new game and reload PASS, version 7
saves, `DELTA=5,1,0` and `8,1,0`, terrain persistence, edge streaming, light
cache, distant terrain, chickens, menus and presets all PASS. The full test
suite passes except the Pillow-dependent texture test (Pillow is not
installed on this machine). The renderer and storage paths the benchmark measured are
unchanged by the merge, so it was not repeated.
