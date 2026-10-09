# Standard benchmark, version 1

The numbered `.cmd` launchers now use the late-1989 8 MB / 16 MHz / 80387 /
200 MB SCSI profile documented in [iso/HARDWARE_1989.md](iso/HARDWARE_1989.md).
This version-1 benchmark and its existing `minimum`/`high` profile names are
retained unchanged for comparable historical measurements. They do not
describe the new launcher minimum; do not relabel their results as 8 MB tests.

`perf_audit.py --towns1989` selects the new 8 MB/16 MHz/80387 profile without
changing the old suite profiles. Add `--hdd <dedicated-marked-scratch-image>`
to exercise HDD terrain streaming; never point this at a save or OS disk.
The result records HDD activation and physical read/write/cache-hit/error counts.

Minimum spec is the priority: MODEL2, 16 MHz, 2 MB, 160x100 rendering,
8-block textured view. Report the higher profile separately: MODEL2,
25 MHz, 4 MB, 320x200 rendering, 12-block textured view. Higher settings
are intentionally demanding; they are not a suggested FPS target.
Keep a higher-end-only optimization only if minimum-spec performance is
neutral and its higher-profile improvement is clear. Never pool the two
profiles into one average. Evaluate hitches and edit latency, not FPS alone.

## Level and phases

`tests/standard_level.inc` defines fixed terrain by coordinates: ground,
underground cave/shafts, water, glass, tree canopies, model flowers and
underground torches. The clear motion lane is z=48 at y=24. Every build
records a whole-terrain hash; comparisons with differing hashes, extents,
resolution, view, RAM or CPU settings are rejected by the suite.

Each phase settles for two emulated seconds, then measures sixteen:

| Step | Workload |
|---|---|
| `look` | Stationary, eight yaw directions |
| `walk` | 32 -> 64 -> 32 X route at four blocks/second |
| `down` | Same route, pitch -250 (maximum down) |
| `up` | Same route, pitch +250 (maximum up) |
| `break` | Remove/restore nearby ground blocks, including chunk seams |
| `mixed` | Motion, maximum down/up and remove/restore edits together |

Motion is a deterministic pose trajectory, not injected player key events.
Physics and AI ticks are frozen so version-dependent movement, mob counts
or random spawns cannot change the scene. Breaking calls real `world_set`,
lighting and mesh publication, bypassing tool durability/mining-hold timing.
Separate keyboard/physics regression tests cover real control behavior.
This is a rendering/terrain-work benchmark, not a complete gameplay or
entity-AI benchmark. Optional `--mobs 12` adds twelve frozen, reproducibly
placed pigs for render-box culling tests.

The default common width is 80 so historical full-world builds fit the
same 2 MB profile without shrinking to a different island. An 80-wide map
fits wholly in the 25-column terrain cache: it tests mesh streaming but
does **not** stress terrain-column eviction. Use width 96 for comparisons
between cached builds on the actual minimum-spec extent. Those runs cross
resident-window boundaries. Do not directly compare 80-wide and 96-wide
results. The benchmark is RAM-backed; floppy seek/read performance is
covered by separate controller/save regressions, not these FPS numbers.

## Run

From `fmtowns/townscraft`, with the bundled Zig, Tsugaru and STUBROM present:

```powershell
# Real correctness regressions, including cached edits and pixel checks:
python tools/run_regressions.py

# Historical comparison, minimum profile, three repeats:
python tools/benchmark_suite.py --refs 4edc6210 1f6d3626 39cf283e --repeats 3

# Before/after integration, actual 96-wide cached extent:
python tools/benchmark_suite.py --refs 159d95f2 39cf283e --width 96 --repeats 3

# A single step, in isolation, against current uncommitted source:
python tools/benchmark_suite.py --refs WORKTREE --step down --width 96 --repeats 3

# Higher settings, kept separate from minimum spec:
python tools/benchmark_suite.py --refs 159d95f2 39cf283e --profile high --repeats 3

# Entity-render culling, same scene on both sides:
python tools/benchmark_suite.py --refs ea931665 e86b3e6f --step look --mobs 12 --repeats 3
```

`--step` also accepts `look`, `walk`, `up`, `break`, `mixed`, or `all`.
An isolated phase starts from a fresh level; `all` runs phases sequentially,
so mesh warming can differ. Compare the same execution mode on both sides.
`--jobs` controls simultaneous builds/runs (default 2). Each repeat rebuilds
a fresh source snapshot. `WORKTREE` is convenient for experiments; use
committed refs when recording a durable result.

The suite writes logs, generated source copies, test ISOs and individual
results under ignored `build/perf-audit/`, plus a summary containing medians,
FPS ranges and raw runs. It never modifies the production ISO. Output names
identify the profile, step, width and mob count; rerunning the same suite
replaces those generated results, so archive important results in `tests/results`.

## Read results correctly

- FPS and exact maximum frame time come from emulated microseconds, not
  host wall time. CPU/RAM profile is explicitly set, not autodetected.
- Median/p95 frame times use 2 ms histogram bins and are lower bounds.
  The last bin represents 510 ms and above; exact maximum remains available.
- Per-phase update/render costs, face/item counts and frames over 66/100 ms
  help distinguish work reduction from simply spreading work across frames.
- Each edit row is `[phase, edit, world_set_us, observed_publication_us,
  update_frames, x, y]`. Observed publication includes intervening rendering
  and polling on the next frame; it is not just the mesher's CPU time.
- Terrain hashes do not prove visual equivalence. Use mesh/pixel regressions,
  scene inspection and missing-mesh checks when changing rendering semantics.
- Interlacing is disabled wherever the historical build supports it, keeping
  full-frame visual work comparable. Music/sky behavior is otherwise retained;
  time is fixed at noon and entities are frozen.
- Small single-run differences are observations, not optimization verdicts.
  Repeat key comparisons. Preserve source refs, fixture version and ISO hash.

For an untouched production build and a genuine gameplay smoke test:

```powershell
python tools/perf_audit.py --name production --ref HEAD --production --build-only
python tools/smoke_production.py build/perf-audit/production
```

The smoke test sends Return through the emulator keyboard, reads matching
ELF symbols, and requires width-96 gameplay with advancing ticks and frames.
It never patches game memory or automatically treats a title screenshot as
successful gameplay. It is a 2 MB / 16 MHz boot check, not an FPS benchmark.
