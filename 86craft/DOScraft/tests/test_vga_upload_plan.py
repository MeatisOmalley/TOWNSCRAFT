"""Pure VGA broadcast-plan correctness; no emulator or target timing claim."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'


class VgaUploadPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if sys.platform == 'win32' and ZIG.is_file():
            compiler = [str(ZIG), 'cc', '-target', 'x86-windows-gnu']
        else:
            native = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
            if not native:
                raise unittest.SkipTest('Native C compiler unavailable')
            compiler = [native]
        temporary = tempfile.TemporaryDirectory(prefix='doscraft-upload-plan-test-')
        cls.addClassCleanup(temporary.cleanup)
        cls.executables = {}
        for path, defines in (('word', []), ('portable', ['-DDOS_VGA_UPLOAD_PLAN_PORTABLE=1'])):
            executable = Path(temporary.name) / f'upload-plan-{path}.exe'
            result = subprocess.run(
                [*compiler, '-std=c99', '-O2', '-fstrict-aliasing', '-fno-builtin',
                 '-Wall', '-Wextra', '-Werror', *defines,
                 '-I', str(ROOT / 'src/platform/dos'),
                 str(ROOT / 'tests/vga_upload_plan_test.c'),
                 str(ROOT / 'src/platform/dos/vga_upload_plan.c'), '-o', str(executable)],
                capture_output=True, text=True, timeout=60)
            if result.returncode:
                raise AssertionError(f'{path} build failed:\n{result.stdout}\n{result.stderr}')
            cls.executables[path] = executable

    def check_native(self, mode, expected):
        for path, executable in self.executables.items():
            with self.subTest(path=path):
                result = subprocess.run([str(executable), mode], capture_output=True,
                                        text=True, timeout=45)
                self.assertEqual(result.returncode, 0, f'{result.stdout}\n{result.stderr}')
                self.assertIn(expected, result.stdout)

    def test_software_vga_patterns_coalescing_byte_counts_strides_and_alignment(self):
        self.check_native('matrix', 'PASS: 8640 matrix cases and 128 random strides/heights')

    def test_single_bit_overlays_never_broadcast_unequal_pixels(self):
        self.check_native('bits', 'PASS: 2560 single-bit differences')

    def test_invalid_inputs_no_writes_and_zero_rows(self):
        self.check_native('invalid', 'PASS: NULL, rows, stride, upfront worst-case capacity')


if __name__ == '__main__':
    unittest.main()
