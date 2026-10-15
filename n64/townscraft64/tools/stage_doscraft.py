"""Stage DOScraft's shipped game source for the N64 into build/doscraft.

DOScraft has no game source tree: tools/build_game.py stages the immutable
Towns snapshot and applies DOScraft's overlays (tools/dos_*.py) during the
build. This script runs that same code from the pinned import
(vendor/doscraft), with the shipped release's flags, and stops before
DOScraft would invoke its DOS compiler:

- build/doscraft/imported/src: Towns core with every shipped overlay, exactly
  as build_game.build() stages it (each overlay replay-checked, as there).
- build/doscraft/platform/src: build_adapters.stage() output for the shipped
  configuration (chicken headers, the selected Towns drawing, save codec and
  sound-asset code).
- build/doscraft/stage.json: flags, overlay recipes and hashes.

Then the N64 adaptations, recorded the same way: libc.c's x86-assembly
memset/memcpy/memmove/memcmp/strlen are removed (newlib provides them), and
fmath.h's x86-assembly mulshift/divshift/muldiv become C, and the 1024-world
medium's word tables are read little-endian (n64_adapt.py).
DOScraft's own adaptation already replaced hw.h with a portable types header.

Usage: python tools/stage_doscraft.py [shipped] [n64]   (default: both)
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import types

sys.dont_write_bytecode = True  # never write __pycache__ into vendor/doscraft/tools
sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I omits the script folder
import n64_adapt  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DOSCRAFT = ROOT / 'vendor/doscraft'

# The shipped 1024-world release: build_game's defaults plus the large-world
# set (tools/prepare_386_vm.py, tools/benchmark_audio.py at 2ce6b443), with
# graphics presets and audio on, as build_game.build(benchmark=False) does.
LARGE_WORLD_FLAGS = ('-DDOS_LARGE_WORLD', '-DDOS_STREAM_LIGHT_CACHE', '-DDOS_FAR_FIELD',
                     '-DDOS_FAR_DETAILS', '-DDOS_FAR_VIEW_DEPTH', '-DDOS_FAR_REPROJECT')

# The N64 game: every DOScraft gameplay overlay (applied unconditionally by
# dos_sources.adapt), the large world with baked light and distant terrain,
# the viewmodel, audio event selection, and the C-level world-code speedups.
# Left out: the 486/VGA presentation and x86 span overlays (planar Mode X
# transport, proven rows, Very Low planar output, trap/texel/row assembly),
# which assume DOS hardware. The N64 replaces the presenter and, later, the
# world rasterizer.
N64_FLAGS = (*LARGE_WORLD_FLAGS, '-DDOS_VIEWMODEL', '-DDOS_STREAM_VALIDATE_ONCE',
             '-DDOS_LIGHT_OFFSETS', '-DDOS_MESH_EDGES', '-DDOS_WORLD_SCANS',
             '-DDOS_BAKED_LIGHT_FAST', '-DDOS_SORT_HISTOGRAMS', '-DDOS_FLAT_INLINE')


class _Staged(Exception):
    """Raised in place of DOScraft's compile step once the transform is known."""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def load_doscraft_tools():
    sys.path.insert(0, str(DOSCRAFT / 'tools'))
    # build_game.build() first writes the sound bank and music pack (data files,
    # not sources). Stub the two builders so staging needs no audio toolchain.
    sys.modules['build_sound_bank'] = types.SimpleNamespace(
        build=lambda path: dict(sha256='', bytes=0, samples=[]))
    sys.modules['build_music_pack'] = types.SimpleNamespace(build=lambda path, **kw: None)
    import build_imported
    import build_adapters
    import build_game
    return build_imported, build_adapters, build_game


def shipped_transform(imported, build_game, scratch, flags):
    """Run build_game.build() up to its staging call and capture the transform."""
    captured = {}

    def stop(output, extra_cflags=(), source_transform=None, **kw):
        captured.update(transform=source_transform, cflags=list(extra_cflags))
        raise _Staged()

    real = imported.build
    imported.build = stop
    try:
        build_game.build(output=scratch,
                         extra_adapter_flags=flags,
                         graphics=True, audio=True)
    except _Staged:
        pass
    finally:
        imported.build = real
    if 'transform' not in captured:
        raise RuntimeError('build_game.build() did not reach its staging step')
    return captured


def apply_transform(imported, transform, folder):
    """Stage the Towns snapshot and apply the overlays, as build_imported.build() does."""
    report = imported.stage_sources(imported.VENDOR, folder)
    for entry in report['files']:
        path = folder / entry['staged']
        before = path.read_bytes()
        after, steps = transform(path.name, before)
        replay = before
        for step in steps:
            old, new = step['before'].encode(), step['after'].encode()
            if replay.count(old) != step['count']:
                raise ValueError(f'overlay recipe does not replay on {path.name}')
            replay = replay.replace(old, new)
        if replay != after:
            raise ValueError(f'overlay is not exactly replayable on {path.name}')
        if steps:
            path.write_bytes(after)
            entry['dos_transformations'] = steps
            entry['dos_staged_sha256'] = sha256(after)
    return report


def n64_libc(path, output):
    step = n64_adapt.libc_portable(path)
    step['file'] = path.relative_to(output).as_posix()
    return step


def stage(imported, adapters, build_game, OUTPUT, flags, prefix):
    """Stage one configuration (DOScraft adapter flags) into OUTPUT."""
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    OUTPUT.mkdir(parents=True)
    captured = shipped_transform(imported, build_game, OUTPUT / 'build-game-scratch', flags)
    report = dict(doscraft_manifest_sha256=sha256((DOSCRAFT / 'MANIFEST.json').read_bytes()),
                  towns_commit=imported.PINNED_COMMIT,
                  adapter_flags=list((*flags, '-DDOS_GAME_AUDIO', '-DDOS_SB_STREAM')),
                  imported_cflags=captured['cflags'])
    report['imported'] = apply_transform(imported, captured['transform'], OUTPUT / 'imported')
    report['platform'] = adapters.stage(OUTPUT / 'platform', large_world=True, chickens=True)
    fmath = OUTPUT / 'imported/src/fmath.h'
    text, step = n64_adapt.fmath_portable(fmath.read_text(encoding='utf-8'))
    fmath.write_text(text, encoding='utf-8')
    report['n64'] = [n64_libc(OUTPUT / 'imported/src/libc.c', OUTPUT), step,
                     *n64_adapt.medium_le32(OUTPUT / 'imported/src', DOSCRAFT / 'src/platform/dos')]
    if prefix == 'N64GAME':
        # The game links raster.c (software renderer); trap.S is src/n64/trap.c.
        report['n64'] += n64_adapt.raster_portable(OUTPUT / 'imported/src')
        report['n64'] += n64_adapt.platform_sources(DOSCRAFT / 'src/platform/dos', OUTPUT / 'n64src')
    shutil.rmtree(OUTPUT / 'build-game-scratch', ignore_errors=True)
    # Lookup tables, generated as DOScraft's build does at compile time.
    tables = OUTPUT / 'imported/src/tables.c'
    subprocess.run([sys.executable, '-I', str(OUTPUT / 'imported/tools/gentables.py'), str(tables)],
                   check=True)
    report['tables_sha256'] = sha256(tables.read_bytes())
    # The game-defining -D flags DOScraft passes to these files, for the Makefile.
    defines = [f for f in captured['cflags'] if f.startswith('-D')]
    # Platform files get the full adapter flag set, as build_game compiles them.
    platform = [*flags, '-DDOS_GAME_AUDIO', '-DDOS_SB_STREAM', '-DDOS_GRAPHICS_PRESETS']
    (OUTPUT / 'defines.mk').write_text(f'{prefix}_DEFINES = ' + ' '.join(defines) + '\n' +
                                       f'{prefix}_PLATFORM_DEFINES = ' + ' '.join(platform) + '\n',
                                       encoding='ascii')
    (OUTPUT / 'stage.json').write_text(json.dumps(report, indent=2, default=str) + '\n',
                                       encoding='utf-8')
    overlaid = [e['staged'] for e in report['imported']['files'] if e.get('dos_transformations')]
    print(f'staged {OUTPUT.name}: {len(report["imported"]["files"])} files, '
          f'{len(overlaid)} with overlays: {", ".join(Path(p).name for p in overlaid)}')


def main(argv):
    imported, adapters, build_game = load_doscraft_tools()
    configs = {
        # Verification copy: byte-identical to DOScraft's shipped build.
        'shipped': (ROOT / 'build/doscraft', (*build_game.DEFAULT_ADAPTER_FLAGS, *LARGE_WORLD_FLAGS),
                    'DOSCRAFT'),
        # The N64 game.
        'n64': (ROOT / 'build/n64game', N64_FLAGS, 'N64GAME'),
    }
    for name in argv or list(configs):
        output, flags, prefix = configs[name]
        stage(imported, adapters, build_game, output, flags, prefix)


if __name__ == '__main__':
    main(sys.argv[1:])
