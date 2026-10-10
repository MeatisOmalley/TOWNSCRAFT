# DOS performance checkpoints

The user authorized optimization work on 2026-10-10, including independent
prioritization beyond the supplied list. This supersedes the old Phase B timing
restriction, **not** the unresolved full gameplay-parity acceptance. Audio is
still deliberately silent. No UI, quality, mob-rule or save-format changes are
authorized as incidental optimizations.

## Automated guest benchmark

Run `tools/benchmark.py --bios <known-good isa486.nvr> --label baseline` with
Python. The existing diagnostic CMOS seed is
`runtime/platform-486dx25-xecixfz2/nvr/isa486.nvr`. Dependencies must already be
installed. It builds separately in `build/bench`, creates a **new** private VM,
boots its ISO-extracted floppy, runs three fresh DOS processes, reads each
`C:\BENCHn.TXT` from the private HDD and closes only its own completed emulator.
No Windows keyboard, focus, capture, BIOS interaction or player save is needed.
A timeout leaves the test open for diagnosis rather than killing active I/O.

Results: `build/bench/486dx25/<label>/latest-report.txt` points to the complete
JSON, including compiler commands, executable/source/ROM/emulator hashes and
all raw samples. Guest PIT channel 2 measures microseconds; unchanged 100 Hz
IRQ0 resolves counter wraps. A native test covers wrap recovery and IRQ-phase
uncertainty. This uses neither host wall-time FPS nor unavailable 486 RDTSC.

Version 1 is a **frozen-simulation graphics/world-update replay**, not full
gameplay FPS. It uses procedural seed 4242, half-resolution view, eight-block
distance, inherited adaptive subdivision and occlusion-off. Six scenes each
have six warm frames then 32 samples: stationary, moving camera, moving full
down, moving full up, a 7x7 stone-house interior and alternating block edits.
Camera positions advance by frame, not elapsed time, for equivalent workloads.
Game physics/mob simulation is frozen; meshes/streaming/raycast/render/HUD and
real VGA upload remain live. Edit calls and initial house construction are
outside frame timing; resulting mesh/light work is inside. This limitation must
be fixed before ranking edit-latency/simulation changes. Native transport tests
and existing real-input gameplay smoke supplement, not replace, this replay.

Stage totals distinguish game/raycast, world updates, collection, entity sort,
page wait, sky, drawing, finishing/doubling, HUD and presentation. Presentation
also separates packing, ISA upload and retrace waits/timeouts. Last-frame VGA
readback and image hashing are outside timed frames. Three repeats expose
variance; compare each scene, p95/max and frame samples, not just aggregate FPS.
Readback proves all four VGA planes match RAM, not renderer visibility itself.

## Initial independent priority

One preliminary run measured about 22 ms packing + 31 ms upload per frame;
retrace was 6–10 ms and **no 100 ms timeout fired**. Drawing was roughly 33 ms
stationary, 41 ms inside the house and 83 ms while looking straight down. These
are diagnostic replay results, not a claim to reproduce every player's scene.

1. Measure one-pass all-plane conversion against four strided framebuffer
   passes. Exact pixels, no gameplay/quality change; additional 57,600 B scratch.
2. Measure exact per-VGA-page unchanged-HUD upload avoidance. Existing HUD
   caching stops RAM drawing, but currently not packing/ISA writes. Comparison
   overhead might erase the gain: keep only measured successes.
3. Investigate draw/setup cost: exact camera setup reuse, remaining signed edge
   divisions, repeated clipping/subdivision setup. Existing shared transforms,
   reciprocal spans, greedy/cached meshes and assembly are already implemented.
4. Then assess conservative occlusion/front-to-back rewrite against house/down
   worst cases. It needs an independent visibility/order reference for cutouts
   and models, not just faster frames. Do not reduce visible content.

Atlas cache conflicts are another code/hardware-specific hypothesis, but
86Box cache fidelity needs checking before treating emulator timing as proof.
HDD/FPU/RAM changes are not automatically helpful: select measured bottlenecks.
Retain unsuccessful candidates as explicitly selectable experiment checkpoints;
never silently leave an unproven candidate in the default playable build.

## Retained first experiment

`--candidate pack-all` selects a single-pass, four-way unrolled converter;
`--candidate reference` keeps the original presenter. Neither changes gameplay
or quality. Its first guest attempt preserved at `runtime/game-play-12ipn3km`
failed the **diagnostic verifier**, not the image hash: the expanded scratch
buffer accidentally made `memcmp` read four planes against a one-plane readback
buffer. The verifier now explicitly compares 19,200 bytes. A native test runs
the actual presenter against an independent four-plane VGA model, checking
every pixel across 18 flips with odd pitches/unaligned source. The separate
converter tests check 448 frames and invalid-input guards. A fresh three-run
guest retest is required before adopting this candidate.
