# Shared textureless distance, 386SX-25

Graphics controls now allow textured distance zero on every CPU. Flat distance
remains at least four blocks and cannot be less than textured distance. The
existing 386 restriction to Low/Very Low is retained. Textureless opaque cube
faces skip affine subdivision; transparent masks retain their geometry.

## Guest replay

Same private 386SX-25, 4 MiB, Trident TVGA8900B, interpreter, compact 1024-block
terrain and Very Low distant terrain as the prior baseline. Resolution 80x50;
flat visibility four blocks. Three post-fixture repeats, six scenes, 32 measured
frames per scene. All repeats PASS; scene hashes are stable within repeats.
Simulation is frozen: these timings measure graphics and background world work,
not complete gameplay throughput. Timing uses the guest PIT.

| Rendering | Mean FPS |
| --- | ---: |
| Original four textured blocks | 6.008 |
| Shared textured distance 0, flat distance 4 | 6.649 |

The setting change raises mean FPS by 10.7%. Images intentionally differ because
opaque faces use average material colors. House and block-edit frames average
4.958 and 4.982 FPS. This is the texture-distance change alone; the additional
coarse distant rendering and mesh/collection experiments are measured separately.

Raw samples, stage timings, source recipes and hardware hashes are retained in
the adjacent JSON. Player VM refresh preserves the complete HDD and CMOS.
