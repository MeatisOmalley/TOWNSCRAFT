"""Build the 1996 executables (Pentium 200, optional 3dfx Voodoo Graphics).

Three executables from one game, for the comparison the 1996 machine is for
(see HARDWARE_1996.md):

  D96REF.EXE   the shipping 386/486 renderer, unchanged, as the baseline
  D96SOFT.EXE  the Pentium software renderer (perspective spans, FDIV overlap)
  D963DFX.EXE  the Voodoo Graphics renderer (own SST-1 driver)

D96SOFT and D963DFX share the release front end (render.c with the R96
overlay) and replace raster.c/trap.S with their own backends. Gameplay,
world storage and audio are the release's.
"""
import argparse
import json
from pathlib import Path

import build_game
import dos_r96

ROOT = build_game.ROOT
OUTPUT = ROOT / 'build/game/p5-200'

# The release's large world, light cache, far field and audio.
RELEASE_WORLD = ('-DDOS_LARGE_WORLD', '-DDOS_STREAM_LIGHT_CACHE', '-DDOS_FAR_FIELD',
                 '-DDOS_FAR_DETAILS', '-DDOS_FAR_VIEW_DEPTH', '-DDOS_FAR_REPROJECT')
# CPU-neutral front end, world and mesh optimizations of the release.
SHARED = ('-fomit-frame-pointer', '-DDOS_STREAM_VALIDATE_ONCE', '-DDOS_LIGHT_OFFSETS',
          '-DDOS_MESH_EDGES', '-DDOS_COLLECT_SWAP', '-DDOS_MEMMOVE_DWORDS', '-DDOS_WORLD_SCANS',
          '-DDOS_BAKED_LIGHT_FAST', '-DDOS_SORT_HISTOGRAMS', '-DDOS_PLANT_BILLBOARDS',
          '-DDOS_VIEWMODEL', *RELEASE_WORLD, '-DDOS_R96')
# Generic VGA transport (Mode X) for the software renderer on any PCI VGA.
# The HUD proof builds on the 486 path's proven doubled rows, so it stays out.
VGA = ('-DDOS_VGA_PACK_ALL', '-DDOS_VGA_HUD_CACHE', '-DDOS_VGA_BROADCAST',
       '-DDOS_VGA_ASYNC_FLIP')
PENTIUM = ('-march=pentium', '-mtune=pentium')

VARIANTS = ('ref', 'soft', '3dfx')
EXE = {'ref': 'D96REF.EXE', 'soft': 'D96SOFT.EXE', '3dfx': 'D963DFX.EXE'}


def variant_flags(variant):
    """Adapter flags of a renderer variant, before the game audio flags."""
    if variant == 'soft':
        return (*SHARED, *VGA)
    if variant == '3dfx':
        return (*SHARED, '-DDOS_R96_3DFX')
    raise ValueError(f'No R96 overlay for variant {variant!r}')


def source_transform(variant, audio=True):
    """The imported sources as a 1996 renderer build sees them."""
    selected = (*variant_flags(variant), *(('-DDOS_GAME_AUDIO', '-DDOS_SB_STREAM') if audio else ()))
    base, _ = build_game.dos_transform(selected, True, audio)

    def transform(name, data):
        data, steps = base(name, data)
        data, more = dos_r96.adapt(name, data)
        return data, [*steps, *more]
    return transform


def soft_adapters(names):
    return (*names, 'r96/r96_soft.c', 'r96/r96_span.S')


def voodoo_adapters(names):
    # The Voodoo presents its own frames: no Mode X transport or VGA packing.
    replaced = {'gfx.c': 'r96/r96_gfx3dfx.c', 'video.c': 'r96/r96_video3dfx.c'}
    return (*(replaced.get(name, name) for name in names),
            'r96/r96_sst1.c', 'r96/r96_raster3dfx.c')


def build(variant, output=None, benchmark=False, extra=()):
    """extra: further adapter flags, e.g. the benchmark's -DDOS_BENCH_QUALITY=n."""
    output = Path(output or OUTPUT / variant)
    if variant == 'ref':
        flags = (*build_game.DEFAULT_ADAPTER_FLAGS, *RELEASE_WORLD, *extra)
        report = build_game.build(output, benchmark=benchmark, extra_adapter_flags=flags,
                                  exe_name=EXE[variant])
    elif variant == 'soft':
        report = build_game.build(output, benchmark=benchmark,
            extra_adapter_flags=(*variant_flags(variant), *extra),
            arch=PENTIUM, source_overlay=dos_r96.adapt, drop_imported=('raster', 'trap'),
            adapter_names=soft_adapters, fpu_adapters=('r96/r96_soft.c',), exe_name=EXE[variant])
    elif variant == '3dfx':
        report = build_game.build(output, benchmark=benchmark,
            extra_adapter_flags=(*variant_flags(variant), *extra),
            arch=PENTIUM, source_overlay=dos_r96.adapt, drop_imported=('raster', 'trap'),
            adapter_names=voodoo_adapters,
            fpu_adapters=('r96/r96_raster3dfx.c', 'r96/r96_sst1.c', 'r96/r96_gfx3dfx.c'),
            exe_name=EXE[variant])
    else:
        raise ValueError(f'Unknown 1996 variant {variant!r}; expected one of {VARIANTS}')
    report['r96_variant'] = variant
    (output / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('variants', nargs='*', choices=(*VARIANTS, []), default=[])
    parser.add_argument('--benchmark', action='store_true', help='Fixed-seed benchmark builds')
    args = parser.parse_args()
    for variant in args.variants or VARIANTS:
        report = build(variant, benchmark=args.benchmark)
        print(f'{variant}: {OUTPUT / variant / EXE[variant]} ({report["executable_bytes"]} bytes)')


if __name__ == '__main__':
    main()
