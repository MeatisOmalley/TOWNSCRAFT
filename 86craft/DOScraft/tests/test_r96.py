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
        for source in (R96 / 'r96_soft.c', R96 / 'r96_span.S', R96 / 'r96_vga.S'):
            obj = folder / (source.stem + '.o')
            run([*flags, '-Werror', '-c', source, '-o', obj])
            objects.append(obj)
        cls.executable = folder / 'r96_soft_test.exe'
        run([*flags, ROOT / 'tests/r96_soft_test.c', *objects, '-o', cls.executable])

    def test_assembly_matches_reference_exact_perspective_and_coverage(self):
        result = run([self.executable, '3000'])
        print(result.stdout.strip())
        self.assertIn('PASS: Mode X plane writer', result.stdout)
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



def find_terrain():
    """A prebaked 1024 world with its distant-terrain map (ignored build data)."""
    name = Path('build/checks/large-world-proto/terrain1024-far.bin')
    for base in (ROOT, Path.home() / 'Documents/craft/86craft/DOScraft'):
        if (base / name).is_file():
            return base / name
    return None


TERRAIN = find_terrain()


@unittest.skipUnless(sys.platform == 'win32' and ZIG is not None and TERRAIN is not None
                     and imported.COMPILER.is_file(),
                     'Zig, DJGPP and the prebaked 1024 world are required')
class WholeFrameTests(unittest.TestCase):
    """Real frames of the prebaked world through the actual 1996 front end."""
    def probe(self, folder, variant):
        import shutil
        output = folder / variant
        build_1996.build(variant, output=output)
        src = output / 'imported/src'
        inc = folder / 'inc'
        inc.mkdir(exist_ok=True)
        (inc / 'dpmi.h').write_text('typedef struct { struct { unsigned short ax,bx,flags; } x; } __dpmi_regs;\n'
                                    'int __dpmi_int(int,__dpmi_regs *);\n')
        common = ['-DDOS_STREAM_LIGHT_CACHE', '-DDOS_FAR_FIELD', '-DDOS_FAR_DETAILS',
                  '-DDOS_FAR_VIEW_DEPTH', '-DDOS_FAR_REPROJECT']
        platform = [*common, '-DDOS_LARGE_WORLD', '-DDOS_R96', '-DDOS_GRAPHICS_PRESETS']
        platform += ['-DR96_SST_HOOK', '-DR96_PROBE_3DFX', '-DDOS_R96_3DFX'] if variant == '3dfx' else ['-DR96_STATS']
        flags = [ZIG, 'cc', '-target', 'x86-windows-gnu', '-mcpu=pentium', '-O2', '-std=gnu99',
                 '-fno-builtin', '-w', '-I', inc, '-I', src, '-I', output / 'platform/src',
                 '-I', ROOT / 'src/platform/dos', '-I', R96, '-I', ROOT / 'tests']
        objects = []
        for name in ('render', 'world', 'textures', 'blocks', 'fmath', 'tables', 'libc'):
            obj = folder / f'{variant}-{name}.o'
            run([*flags, *common, '-c', src / f'{name}.c', '-o', obj])
            objects.append(obj)
        backend = (('r96_raster3dfx.c', 'r96_sst1.c') if variant == '3dfx'
                   else ('r96_soft.c', 'r96_span.S'))
        sources = [ROOT / 'src/platform/dos/heap.c', ROOT / 'src/platform/dos/graphics_presets.c',
                   *(R96 / name for name in backend)]
        for source in sources:
            obj = folder / f'{variant}-{source.stem}.o'
            run([*flags, *platform, '-c', source, '-o', obj])
            objects.append(obj)
        obj = folder / f'{variant}-hdd.o'
        run([*flags, *platform, '-Dopen=world_test_open', '-c', ROOT / 'src/platform/dos/hdd.c', '-o', obj])
        exe = folder / f'probe-{variant}.exe'
        run([*flags, *platform, ROOT / 'tests/r96_frame_probe.c', *objects, obj, '-o', exe])
        terrain = folder / 'terrain.bin'
        if not terrain.exists():
            shutil.copyfile(TERRAIN, terrain)
        (folder / 'png').mkdir(exist_ok=True)
        return exe, terrain

    def test_software_assembly_equals_reference_on_real_frames(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-r96-frames-') as directory:
            folder = Path(directory)
            exe, terrain = self.probe(folder, 'soft')
            for view in ('16', '32'):
                sums = []
                for reference in (False, True):
                    env = dict(os.environ)
                    env.pop('R96_REF', None)
                    if reference:
                        env['R96_REF'] = '1'
                    result = subprocess.run([str(exe), str(terrain), str(folder / 'png'), view],
                                            capture_output=True, text=True, env=env, timeout=600)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    sums.append([line.split()[1] for line in result.stdout.splitlines()
                                 if line.startswith('p') and ' sum=' in line])
                self.assertEqual(len(sums[0]), 20)
                self.assertEqual(sums[0], sums[1], f'view {view}: assembly frames differ from C reference')

    def test_voodoo_backend_renders_real_frames(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-r96-frames3d-') as directory:
            folder = Path(directory)
            exe, terrain = self.probe(folder, '3dfx')
            result = run([exe, terrain, folder / 'png', '24'], timeout=600)
            frames = [line for line in result.stdout.splitlines() if line.startswith('p')]
            self.assertEqual(len(frames), 20)
            self.assertTrue(all(int(line.split('triangles=')[1].split()[0]) >= 0 for line in frames))
            self.assertTrue((folder / 'png/v1-1.png').stat().st_size > 1000)


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
                         'r96_span.S', 'r96_vga.S'):
                flags = integer if name == 'r96_video3dfx.c' else fpu
                obj = folder / (Path(name).stem + '.o')
                run([*flags, '-c', R96 / name, '-o', obj])
                self.assertTrue(obj.stat().st_size > 0)


if __name__ == '__main__':
    unittest.main()
