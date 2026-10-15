# Shared rendering optimizations, 386SX-25

Settings: textured distance **0**, flat distance **4**, resolution **80x50**,
distant terrain **Very Low**. The original baseline textures all four blocks.
The 386 keeps its Low/Very Low preset restriction and Very Low startup default.

All new runtime predicates use the shared distance/resolution settings, without
CPU checks. Opaque textureless faces skip subdivision. Cutout masks remain.
At 80x50, Very Low distant terrain uses four displayed texels per strip instead
of two, reducing heightmap ray marches. Background light/seam mesh refreshes
outside the view plus two blocks defer until approached. Geometry edits and full
drains retain priority. Streaming uses a two-block margin at this resolution.
Wide collection reuse is scoped to 80x50, with axis/boundary/tier invalidation
and tight-plane fallback on list saturation.

## Performance evidence

Same interpreter, 386SX-25, 4 MiB, Trident TVGA8900B, ROMs and compact 1024-block
world as the original baseline. Three post-fixture repeats; six scenes;
32 measured frames per scene. All three repeats PASS with stable scene hashes.
Guest PIT timings. Simulation is frozen; this is graphics/background-world
throughput, not a complete gameplay benchmark. No frame dumps or reference
redraws were included in these timings. Images intentionally differ.

| Scene | Original FPS | Textureless alone | Shared optimizations |
| --- | ---: | ---: | ---: |
| open_static | 8.366 | 8.386 | 10.379 |
| walk | 6.018 | 6.770 | 7.962 |
| walk_down | 5.904 | 7.251 | 9.031 |
| walk_up | 9.510 | 10.514 | 10.511 |
| house | 4.557 | 4.958 | 6.332 |
| block_edits | 4.567 | 4.982 | 5.733 |
| Overall | 6.008 | 6.649 | 7.893 |

Overall FPS increases **31.4%** with the explicit textureless setting.
Edit calls also cost 75.2 ms on average, giving 4.005 complete edit/render
cycles per second; edit-call cost is not included in the render FPS column.
Raw samples, source recipes and runtime hashes are in the adjacent JSON.

## Validation

13 focused menu, graphics-policy, shader, mesh-scheduler, fill-boundary and
source-replay checks pass. Standard and large-world production builds link.
An unrelated preexisting `test_render_exact` source-count assertion expects
two DOS_TEXEL_EDX guards; the merged assembly has three. Its host parity checks
also skip because gcc -m32 is unavailable. No unrelated test repairs were made.

Reference redraw/image diagnostics are separate from the performance runs.
The ordinary fixed arenas lack room for the reference-redraw saved buffers;
the existing memory-report sizing also exits during initial world allocation.
The benchmark-only reference-redraw build reserves 192 KiB of scratch; it does
not change production arenas or emulated installed RAM. Failed diagnostic files
are retained separately, with no performance claim.

The final scratch-enabled diagnostic PASSes with **228 reference redraw frames
and zero collection mismatches**. Six framebuffer dumps were produced. Walk
and house images were visually reviewed: flat material colors, intact doorway
masks and HUD, with deliberate wider distant strips. Diagnostic timings are
excluded from performance claims.

- Diagnostic VM: `runtime/bench-386-ewtnws8y`
- Walk image: `build/checks/386-optimization-visuals/shared-B1S1.png`
- House image: `build/checks/386-optimization-visuals/shared-B1S4.png`
- Installed player VM: `runtime/game-386-3fgbr71j`
- HDD preserved SHA-256: `3e945c43c2c36c6c6adc85f77ef5561bda4c88cad13faf257b4b8d949f1f946d`
- Production executable SHA-256: `c191208fffa8d3c2764a5b9943d416e86ddba6da1714f6a5cac9dafc79577b0c`

Both production executables were rebuilt after the diagnostic scratch change;
their binaries are byte-for-byte unchanged by that benchmark-only addition.
The 386 launcher validation passes; completed/failed diagnostic VMs are closed,
with disks and reports retained.
