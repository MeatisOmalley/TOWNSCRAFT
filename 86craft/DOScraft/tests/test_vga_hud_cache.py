"""Native exact HUD-cache correctness; guest comparison cost remains unmeasured."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class VgaHudCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if sys.platform == 'win32' and ZIG.is_file():
            compiler = [str(ZIG), 'cc', '-target', 'x86-windows-gnu']
        else:
            native = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
            if not native:
                raise unittest.SkipTest('Native C compiler unavailable')
            compiler = [native]
        temporary = tempfile.TemporaryDirectory(prefix='doscraft-hud-cache-test-')
        cls.addClassCleanup(temporary.cleanup)
        cls.executables = {}
        for path, defines in (('word', []), ('portable', ['-DDOS_VGA_HUD_CACHE_PORTABLE=1'])):
            executable = Path(temporary.name) / f'hud-cache-{path}.exe'
            result = subprocess.run(
                [*compiler, '-std=c99', '-O2', '-fstrict-aliasing', '-fno-builtin',
                 '-Wall', '-Wextra', '-Werror', *defines,
                 '-I', str(ROOT / 'src/platform/dos'),
                 str(ROOT / 'tests/vga_hud_cache_test.c'),
                 str(ROOT / 'src/platform/dos/vga_hud_cache.c'), '-o', str(executable)],
                capture_output=True, text=True, timeout=60)
            if result.returncode:
                raise AssertionError(f'{path} build failed:\n{result.stdout}\n{result.stderr}')
            cls.executables[path] = executable

    def check_native(self, mode, expected):
        for path, executable in self.executables.items():
            with self.subTest(path=path):
                result = subprocess.run([str(executable), mode], capture_output=True,
                                        text=True, timeout=30)
                self.assertEqual(result.returncode, 0, f'{result.stdout}\n{result.stderr}')
                self.assertIn(expected, result.stdout)

    def test_unaligned_matrix_partial_lengths_exact_update_and_guards(self):
        self.check_native('matrix', 'PASS: 4864 alignment/length combinations')

    def test_all_single_bit_changes_at_every_byte(self):
        self.check_native('bits', 'PASS: all 25600 single-bit changes')

    def test_invalid_and_zero_length_semantics(self):
        self.check_native('invalid', 'PASS: NULL, bounded length, zero-length')

    def test_protected_pages_no_overread_or_write_on_equality(self):
        self.check_native('pages', 'PASS: protected tails, read-only equality')


if __name__ == '__main__':
    unittest.main()
