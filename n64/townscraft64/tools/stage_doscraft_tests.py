"""Stage DOScraft's native tests as N64 test ROM sources in build/dtests.

Each test is staged exactly as its DOScraft driver (vendor/doscraft/tests/
test_*.py) stages it: the same Towns snapshot, the same overlays and the
same source files. Then the N64 adaptations from n64_adapt.py are applied.
Writes build/dtests/tests.mk (ROM targets for the Makefile) and
build/dtests/expect.json (expected outcome per ROM, for run_tests.py).

stream_light_native.c runs against the compact 1024 world in the ROM (see
FIXTURE). far_world_native.c is not staged yet.
"""
import json
from pathlib import Path
import shutil
import sys

sys.dont_write_bytecode = True  # never write __pycache__ into vendor/doscraft/tools
sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I omits the script folder
import n64_adapt  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DOSCRAFT = ROOT / 'vendor/doscraft'
TOWNS_TESTS = ROOT / 'vendor/towns-tests/tests'
OUTPUT = ROOT / 'build/dtests'
PLATFORM = 'vendor/doscraft/src/platform/dos'
# DOScraft's compact V2 crown-leaf world: tools/convert_large_v2.py at c4ce3b7b
# applied to its shipped bake terrain1024-crown-leaves.bin reproduces exactly
# this file (SHA-256 below). Not committed (33.8 MB); stage it with
# tools/stage_doscraft_tests.py after placing it at FIXTURE.
FIXTURE = ROOT / 'build/fixtures/terrain1024-crown-leaves-v2.bin'
FIXTURE_SHA256 = '45d5e9a149c55b994f7f1505c96b094b227010417d9dd280544feeb97fb6093d'
MOB_CODEC_CASES = ('roundtrip', 'empty_capacity', 'malformed_headers', 'malformed_records',
                   'malformed_ranges', 'valid_boundaries', 'truncation')


def stage(imported, name):
    folder = OUTPUT / name
    imported.stage_sources(imported.VENDOR, folder)
    return folder, folder / 'src'


def main():
    sys.path.insert(0, str(DOSCRAFT / 'tools'))
    import build_imported as imported
    import dos_sources
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    roms, expect, recipes = [], {}, {}

    def rel(path):
        return str(Path(path).relative_to(ROOT).as_posix()) if Path(path).is_absolute() else str(path)

    def rom(name, folder, sources, includes, arg=None, expect_ok=True, contains='PASS', dfs=None,
            timeout=None):
        roms.append(dict(name=name, sources=[rel(s) for s in sources], includes=[rel(i) for i in includes],
                         arg=arg, dfs=rel(dfs) if dfs else None))
        expect[name] = dict(exit_zero=expect_ok, contains=contains)
        if timeout:
            expect[name]['timeout'] = timeout

    # chickens: tests/test_chickens.py - default overlays on five files.
    folder, src = stage(imported, 'chickens')
    for name in ('blocks.h', 'blocks.c', 'mobs.h', 'mobs.c', 'inventory.c'):
        path = src / name
        path.write_bytes(dos_sources.adapt(name, path.read_bytes())[0])
    recipes['chickens'] = n64_adapt.adapt_staged(src)
    shutil.copy(DOSCRAFT / 'tests/chickens_native.c', folder / 'chickens_native.c')
    rom('dt_chickens', folder, [folder / 'chickens_native.c', *(src / f'{n}.c' for n in
        ('blocks', 'inventory', 'physics', 'fmath', 'libc', 'tables'))], [src, PLATFORM],
        contains='PASS: chicken spawn')

    # mob codec: tests/test_mob_codec.py - the byte-identical pinned mobs.c.
    folder, src = stage(imported, 'mob_codec')
    recipes['mob_codec'] = n64_adapt.adapt_staged(src)
    shutil.copy(DOSCRAFT / 'tests/mob_codec_test.c', folder / 'mob_codec_test.c')
    for case in MOB_CODEC_CASES:
        rom(f'dt_mob_codec_{case}', folder, [folder / 'mob_codec_test.c', *(src / f'{n}.c' for n in
            ('blocks', 'fmath', 'libc', 'tables'))], [src], arg=case,
            contains=f'PASS: pinned Towns mobs codec {case}')

    # immediate light: tests/test_immediate_light.py - the original world.c
    # must fail and DOScraft's overlaid world.c must pass. Deviation: the
    # driver overlays world.c alone, which no longer compiles at c4ce3b7b
    # (dos_block_ids makes world.c need the overlaid blocks.h), so "fixed"
    # applies the default overlays to every staged file.
    for label, overlay, ok, message in (
            ('baseline', False, False, 'FAIL: placed torch lighting was deferred'),
            ('fixed', True, True, 'PASS: torch placement and removal')):
        folder, src = stage(imported, f'immediate_light_{label}')
        if overlay:
            for path in sorted(src.iterdir()):
                if path.is_file():
                    path.write_bytes(dos_sources.adapt(path.name, path.read_bytes())[0])
        recipes[f'immediate_light_{label}'] = n64_adapt.adapt_staged(src)
        tests = folder / 'tests'
        tests.mkdir()
        shutil.copy(TOWNS_TESTS / 'edit_test.c', tests / 'edit_test.c')
        shutil.copy(DOSCRAFT / 'tests/immediate_light_native.c', tests / 'immediate_light_native.c')
        shutil.copy(TOWNS_TESTS / 'no_hdd.c', tests / 'no_hdd.c')
        rom(f'dt_immediate_light_{label}', folder,
            [tests / 'immediate_light_native.c', tests / 'no_hdd.c',
             *(src / f'{n}.c' for n in ('blocks', 'fmath', 'libc', 'tables'))], [src],
            expect_ok=ok, contains=message)

    # stream light: tests/test_stream_light.py - every file overlaid with
    # large_world and stream_light, a dpmi.h stub, and the real heap. N64
    # changes: the medium is read little-endian (n64_adapt.medium_le32), the
    # sector backend is src/n64/rom_hdd.c over the medium in the ROM's DFS in
    # place of DOS hdd.c, and the arenas are DOScraft's own 8 MiB tier (672 KiB
    # + 3 MiB, system.c) in place of the test's 9 MiB, which the N64 cannot
    # hold. The medium is DOScraft's compact V2 crown-leaf world (FIXTURE).
    if FIXTURE.is_file():
        folder, src = stage(imported, 'stream_light')
        for path in sorted(src.iterdir()):
            if path.is_file():
                path.write_bytes(dos_sources.adapt(path.name, path.read_bytes(),
                                                   large_world=True, stream_light=True)[0])
        recipes['stream_light'] = [*n64_adapt.adapt_staged(src),
                                   *n64_adapt.medium_le32(src, DOSCRAFT / 'src/platform/dos')]
        (folder / 'dpmi.h').write_text('typedef struct { struct { unsigned short ax,bx,flags; } x; } '
                                       '__dpmi_regs;\nint __dpmi_int(int, __dpmi_regs *);\n')
        (folder / 'io.h').write_text('/* N64: DOS <io.h> stub; _commit is in n64_dos_glue.c. */\n'
                                     'int _commit(int);\n')
        text = (DOSCRAFT / 'tests/stream_light_native.c').read_text(encoding='utf-8')
        fixture_steps = []
        for before, after in (
                ('u8 *arena=calloc(9*1048576u,1);', 'u8 *arena=calloc((672u+3072u)*1024u,1);'),
                ('!dos_heap_init(arena,1048576,arena+1048576,8*1048576)',
                 '!dos_heap_init(arena,672u*1024u,arena+672u*1024u,3072u*1024u)')):
            if text.count(before) != 1:
                raise ValueError(f'stream_light_native.c changed: {before}')
            text = text.replace(before, after)
            fixture_steps.append(dict(operation='replace', file='stream_light_native.c',
                                      before=before, after=after))
        recipes['stream_light'] += fixture_steps
        (folder / 'stream_light_native.c').write_text(text, encoding='utf-8')
        dfs = folder / 'dfs'
        dfs.mkdir()
        if n64_adapt.sha256(FIXTURE.read_bytes()) != FIXTURE_SHA256:
            raise ValueError(f'{FIXTURE} is not the pinned compact world')
        shutil.copy(FIXTURE, dfs / 'terrain.bin')
        rom('dt_stream_light', folder,
            [folder / 'stream_light_native.c', ROOT / 'tests/n64_dos_glue.c', ROOT / 'src/n64/rom_hdd.c',
             ROOT / PLATFORM / 'heap.c', *(src / f'{n}.c' for n in ('blocks', 'fmath', 'libc', 'tables'))],
            # vendor/doscraft/tests resolves the test's "../src/platform/dos/heap.h".
            [folder, src, PLATFORM, 'vendor/doscraft/tests'], arg='rom:/terrain.bin',
            contains='PASS: canonical baked light',
            dfs=dfs, timeout=900)
    else:
        print(f'skipping dt_stream_light: {FIXTURE} missing (see research/n2b note)')

    # Lookup tables for every staged copy, as the drivers generate them.
    import subprocess
    for folder in OUTPUT.iterdir():
        subprocess.run([sys.executable, '-I', str(folder / 'tools/gentables.py'),
                        str(folder / 'src/tables.c')], check=True)

    lines = ['# Generated by tools/stage_doscraft_tests.py', f'DTEST_NAMES = {" ".join(r["name"] for r in roms)}',
             f'DTEST_DFS_NAMES = {" ".join(r["name"] for r in roms if r["dfs"])}']
    for r in roms:
        lines.append(f'DTEST_SRC_{r["name"]} = {" ".join(r["sources"])}')
        lines.append(f'DTEST_INC_{r["name"]} = {" ".join("-I" + i for i in r["includes"])}')
        lines.append(f'DTEST_ARG_{r["name"]} = {r["arg"] or ""}')
        lines.append(f'DTEST_DFS_{r["name"]} = {r["dfs"] or ""}')
    (OUTPUT / 'tests.mk').write_text('\n'.join(lines) + '\n', encoding='ascii')
    (OUTPUT / 'expect.json').write_text(json.dumps(expect, indent=2) + '\n', encoding='utf-8')
    (OUTPUT / 'stage.json').write_text(json.dumps(dict(roms=roms, n64_adaptations=recipes),
                                                  indent=2) + '\n', encoding='utf-8')
    print(f'staged {len(roms)} DOScraft test ROMs: {", ".join(r["name"] for r in roms)}')


if __name__ == '__main__':
    main()
