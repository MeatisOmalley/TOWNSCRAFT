"""Native all-plane conversion correctness; no VGA or guest speed claim."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class VgaPackAllTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if sys.platform == 'win32' and ZIG.is_file():
            compiler = [str(ZIG), 'cc', '-target', 'x86-windows-gnu']
        else:
            native = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
            if not native:
                raise unittest.SkipTest('Native C compiler unavailable')
            compiler = [native]
        temporary = tempfile.TemporaryDirectory(prefix='doscraft-vga-all-test-')
        cls.addClassCleanup(temporary.cleanup)
        cls.executable = Path(temporary.name) / 'vga-pack-all-test.exe'
        result = subprocess.run(
            [*compiler, '-std=c99', '-O2', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
             '-I', str(ROOT / 'src/platform/dos'), str(ROOT / 'tests/vga_pack_all_test.c'),
             str(ROOT / 'src/platform/dos/vga_pack_all.c'), '-o', str(cls.executable)],
            capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(f'Native build failed:\n{result.stdout}\n{result.stderr}')

    def native(self, mode):
        result = subprocess.run([str(self.executable), mode], capture_output=True,
                                text=True, timeout=30)
        self.assertEqual(result.returncode, 0, f'{result.stdout}\n{result.stderr}')
        return result.stdout

    def test_independent_planes_pitches_all_bytes_unaligned_and_guards(self):
        self.assertIn('PASS: 448 frames, independent four-plane reference', self.native('valid'))

    def test_invalid_inputs_leave_all_buffers_untouched(self):
        self.assertIn('rejected before any write', self.native('invalid'))


if __name__ == '__main__':
    unittest.main()
