# Claude optimization tier audit — 2026-10-13

Audited the latest remote Claude tip `da1d94b4` (including his new row-fill
production-default commit), together with the accepted DOS features.
All fourteen successful optimizations are enabled in the production flags.
Runtime layout and prerequisites determine when their code applies; a main
preset does not disable the invisible world/renderer optimizations.

| Optimization / flag | Active settings / prerequisite |
| --- | --- |
| Stream validation once (`DOS_STREAM_VALIDATE_ONCE`) | All tiers; streamed column installation keeps one mandatory checksum |
| Light neighbor offsets (`DOS_LIGHT_OFFSETS`) | All tiers |
| Fast meshing at column edges (`DOS_MESH_EDGES`) | All tiers |
| Collect plane pointer swap (`DOS_COLLECT_SWAP`) | All tiers with the existing collect-margin overlay |
| Three histogram counts in one pass (`DOS_SORT_HISTOGRAMS`) | All tiers |
| Skip nonresident world scans (`DOS_WORLD_SCANS`) | All tiers |
| Dword overlapping memmove (`DOS_MEMMOVE_DWORDS`) | All tiers |
| Avoid redundant baked-light clear (`DOS_BAKED_LIGHT_FAST`) | All tiers with the existing stream-light cache |
| EDX texture fraction (`DOS_TEXEL_EDX`) | All textured raster layouts, including full resolution and Very Low/Potato |
| Row gradient multiply (`DOS_ROW_MUL`) | All textured raster layouts; its measured i486 timing gain is specific to 86Box |
| Carry row pointers (`DOS_ROW_POINTER`) | All raster layouts |
| Inline short flat fills (`DOS_FLAT_INLINE`) | All tiers, chunky and planar |
| Inline sky/fog row fills (`DOS_FILL_ROWS`) | Compiled by default; executes on the planar 160x100 target, normally Low/Medium. Chunky High/Very Low/Potato retain their existing row fill. Skips its source overlay if a diagnostic build omits the planar prerequisite. |
| Single plant billboard (`DOS_PLANT_BILLBOARDS`) | 80x100 and 80x50 (Very Low/Potato), including manual resolution selection. Standard 160x100/320x200 retain the original crossed quads. Independent of distant-terrain setting. |

## Rejected experiments remain disabled

These are not successful optimizations and have no enabled production tier:

- `DOS_MULSHIFT`: exact but slower due to correction work/register spills.
- `DOS_PLANAR_EXPAND_WORDS`: slower finish stage (about +0.15 ms).
- `DOS_VGA_SCAN_QUAD`: display corruption in roughly 12–20% of captured play
  frames and no reliable net frame-time improvement.

Their source and original evidence remain available for experiments. Half-rate
sky animation was an ablation, not an implemented successful optimization.
No discarded experiment is silently enabled by a preset.

## Leaf opacity verified

Both full and small-view `T_LEAVES` contain no zero/transparent texels. The
importer fills source alpha holes with dark green. The actual renderer metadata
refresh produces `texTransparent[T_LEAVES] == 0` after repeated resolution
switches. `B_LEAVES` has `BF_SOLID | BF_OPAQUE | BF_CUBE`, including metadata
byte 71 used for distant exclusion. The mesher's opaque-neighbor table and
`face_visible()` hide adjacent faces. Dark pattern gaps are solid pixels.
Distant leaf patterns likewise paint two opaque palette colors. No leaf art,
lighting, gameplay or storage changes are needed.

## Validation

Production flag allowlist and emitted-source checks cover all fourteen flags
and exclude the three failures. Native tests cover the real plant function
(one facing quad at low resolution, original crossed output otherwise),
inline planar row-fill bounds, actual Potato raster/assembly, and both texture
sets plus repeated renderer metadata refresh. Exact release smoke and shared
ISO publication details are recorded alongside this audit.
