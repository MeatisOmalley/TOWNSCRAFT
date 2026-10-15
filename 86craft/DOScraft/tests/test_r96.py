"""1996 renderer: native x86 checks of the Pentium span loops and overlay."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
R96 = ROOT / 'src/platform/r96'
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported
import build_1996
import dos_r96

ZIG_RELATIVE = Path('fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe')


def find_zig():
    """The pinned Zig of the craft checkout; a worktree may sit elsewhere."""
    candidates = [Path(os.environ['DOSCRAFT_ZIG'])] if os.environ.get('DOSCRAFT_ZIG') else []
    candidates += [parent / ZIG_RELATIVE for parent in ROOT.parents]
    candidates.append(Path.home() / 'Documents/craft' / ZIG_RELATIVE)
    return next((path for path in candidates if path.is_file()), None)


ZIG = find_zig()


def run(command, timeout=300):
    command = list(map(str, command))
    result = subprocess.run(command, capture_output=True, text=True,
                            errors='replace', timeout=timeout)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result


def stage(folder, variant='soft'):
    """Imported sources transformed as a 1996 renderer build transforms them."""
    imported.stage_sources(imported.VENDOR, folder / 'imported')
    src = folder / 'imported/src'
    transform = build_1996.source_transform(variant)
    for name in ('raster.h', 'render.c'):
        before = (src / name).read_bytes()
        after, steps = transform(name, before)
        replay = before
        for step in steps:
            old, new = step['before'].encode(), step['after'].encode()
            if replay.count(old) != step['count']:
                raise AssertionError(f'R96 recipe does not replay on {name}')
            replay = replay.replace(old, new)
        if replay != after or after == before:
            raise AssertionError(f'Expected an exact R96 overlay of {name}')
        (src / name).write_bytes(after)
    return src


class OverlayTests(unittest.TestCase):
    def test_overlay_applies_to_the_release_render_source(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-r96-overlay-') as directory:
            src = stage(Path(directory))
            text = (src / 'render.c').read_text()
            self.assertIn('raster_poly(sv,m,tex,flags|RP_PERSP);', text)
            self.assertEqual(text.count('=r96Inv;'), 2)
            self.assertIn('\tr96_textures_changed();\n}', text)
            self.assertIn('int w;', (src / 'raster.h').read_text())

    def test_unrelated_sources_are_untouched(self):
        data = b'int main(void){return 0;}\n'
        self.assertEqual(dos_r96.adapt('game.c', data), (data, []))


@unittest.skipUnless(sys.platform == 'win32' and ZIG is not None,
                     'Pinned native x86 Windows Zig harness unavailable')
class SoftwareSpanNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix='doscraft-r96-soft-')
        cls.addClassCleanup(temporary.cleanup)
        folder = Path(temporary.name)
        src = stage(folder)
        flags = [ZIG, 'cc', '-target', 'x86-windows-gnu', '-mcpu=pentium',
                 '-O2', '-std=gnu99', '-fno-builtin', '-Wall', '-Wextra',
                 '-Wno-unused-function', '-I', src, '-I', ROOT / 'src/platform/dos',
                 '-I', R96]
        objects = []
        for source in (R96 / 'r96_soft.c', R96 / 'r96_span.S'):
            obj = folder / (source.stem + '.o')
            run([*flags, '-Werror', '-c', source, '-o', obj])
            objects.append(obj)
        cls.executable = folder / 'r96_soft_test.exe'
        run([*flags, ROOT / 'tests/r96_soft_test.c', *objects, '-o', cls.executable])

    def test_assembly_matches_reference_exact_perspective_and_coverage(self):
        result = run([self.executable, '3000'])
        print(result.stdout.strip())
        self.assertIn('PASS: 3000 perspective scenes', result.stdout)



@unittest.skipUnless(sys.platform == 'win32' and ZIG is not None,
                     'Pinned native x86 Windows Zig harness unavailable')
class VoodooBackendNativeTests(unittest.TestCase):
    """The Voodoo backend and driver against a model of the SST-1 board."""
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix='doscraft-r96-3dfx-')
        cls.addClassCleanup(temporary.cleanup)
        folder = Path(temporary.name)
        src = stage(folder)
        flags = [ZIG, 'cc', '-target', 'x86-windows-gnu', '-mcpu=pentium',
                 '-O2', '-std=gnu99', '-fno-builtin', '-Wall', '-Wextra',
                 '-Wno-unused-function', '-DR96_SST_HOOK', '-I', src,
                 '-I', ROOT / 'src/platform/dos', '-I', R96]
        objects = []
        for source in (R96 / 'r96_raster3dfx.c', R96 / 'r96_sst1.c'):
            obj = folder / (source.stem + '.o')
            run([*flags, '-Werror', '-c', source, '-o', obj])
            objects.append(obj)
        cls.executable = folder / 'r96_3dfx_test.exe'
        run([*flags, ROOT / 'tests/r96_3dfx_test.c', *objects, '-o', cls.executable])

    def test_textures_coverage_fills_far_layer_and_affine(self):
        result = run([self.executable])
        print(result.stdout.strip())
        self.assertIn('PASS: 1500 Voodoo scenes, fills, far layer, affine', result.stdout)


@unittest.skipUnless(imported.COMPILER.is_file(), 'Existing DJGPP unavailable')
class DjgppObjectTests(unittest.TestCase):
    """The 1996 sources compile for the Pentium DOS target as built."""
    def test_pentium_objects(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-r96-dos-') as directory:
            folder = Path(directory)
            src = stage(folder)
            # As build_1996: the backends may use the x87, the rest never does.
            integer = [imported.COMPILER, *imported.CFLAGS, '-march=pentium', '-mtune=pentium',
                       '-DDOS_R96', '-Werror', '-I', src, '-I', ROOT / 'src/platform/dos', '-I', R96]
            fpu = [flag for flag in integer if flag not in ('-mno-80387', '-mgeneral-regs-only')]
            # r96_gfx3dfx.c needs the staged gfx drawing include: build_1996 covers it.
            for name in ('r96_soft.c', 'r96_raster3dfx.c', 'r96_sst1.c', 'r96_video3dfx.c',
                         'r96_span.S'):
                flags = integer if name == 'r96_video3dfx.c' else fpu
                obj = folder / (Path(name).stem + '.o')
                run([*flags, '-c', R96 / name, '-o', obj])
                self.assertTrue(obj.stat().st_size > 0)


if __name__ == '__main__':
    unittest.main()
