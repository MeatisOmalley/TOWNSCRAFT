from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported
import dos_sources

ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
TABLES = ROOT.parents[1] / 'fmtowns/townscraft/build/tables.c'
EDIT = ROOT.parents[1] / 'fmtowns/townscraft/tests/edit_test.c'
NO_HDD = ROOT.parents[1] / 'fmtowns/townscraft/tests/no_hdd.c'


class ImmediateLightTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file() and TABLES.is_file(), 'native Zig and generated Towns tables required')
    def test_torch_edit_finishes_stream_light_first(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-light-') as folder:
            folder = Path(folder)
            for label, overlay, passing in [('baseline', False, False),
                                            ('fixed', True, True)]:
                # The baseline keeps the untouched Towns source; the fixed build
                # applies the default DOS overlays to every staged file, headers
                # included, as the game build does.
                staged = folder / label
                build_imported.stage_sources(build_imported.VENDOR, staged)
                src = staged / 'src'
                if overlay:
                    for path in src.iterdir():
                        path.write_bytes(dos_sources.adapt(path.name, path.read_bytes())[0])
                # edit_test.c includes ../src/world.c.
                tests = staged / 'tests'
                tests.mkdir()
                shutil.copyfile(EDIT, tests / 'edit_test.c')
                shutil.copyfile(ROOT / 'tests/immediate_light_native.c', tests / 'immediate_light_native.c')
                exe = folder / (label + '.exe')
                command = [str(ZIG), 'cc', '-target', 'x86-windows-gnu', '-O2',
                           '-std=gnu99', '-fno-builtin', '-Wno-unused-function',
                           '-I', str(src), str(tests / 'immediate_light_native.c'),
                           *(str(src / name) for name in ('blocks.c', 'fmath.c', 'libc.c')),
                           str(TABLES), str(NO_HDD), '-o', str(exe)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-3000:])
                result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode == 0, passing, result.stdout + result.stderr[-3000:])
                self.assertIn('PASS: torch placement and removal' if passing else
                              'FAIL: placed torch lighting was deferred', result.stdout)


if __name__ == '__main__':
    unittest.main()
