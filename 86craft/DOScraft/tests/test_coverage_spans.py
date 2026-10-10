"""Bounded exact coverage correctness; no renderer adoption or 486 speed claim."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'src/platform/dos'
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported


def run(command, timeout=60):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True,
                            errors='replace', timeout=timeout)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result


class CoverageSpansNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        win32_zig = sys.platform == 'win32' and ZIG.is_file()
        if win32_zig:
            compiler = [ZIG, 'cc', '-target', 'x86-windows-gnu', '-mcpu=i486']
        else:
            native = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
            if not native:
                raise unittest.SkipTest('Native C compiler unavailable')
            compiler = [native]
        temporary = tempfile.TemporaryDirectory(prefix='doscraft-coverage-native-')
        cls.addClassCleanup(temporary.cleanup)
        cls.executables = {}
        for name, defines in (('bsf', []), ('portable', ['-DDOS_COVERAGE_PORTABLE=1'])):
            executable = Path(temporary.name) / f'coverage-{name}.exe'
            run([*compiler, '-std=c99', '-O2', '-fstrict-aliasing', '-fno-builtin',
                 '-Wall', '-Wextra', '-Werror', *defines, '-I', PLATFORM,
                 ROOT / 'tests/coverage_spans_test.c', PLATFORM / 'coverage_spans.c',
                 '-o', executable])
            if win32_zig:
                data = executable.read_bytes()
                if data[:2] != b'MZ':
                    raise AssertionError('Native test executable must be PE')
                pe = struct.unpack_from('<I', data, 0x3c)[0]
                if (data[pe:pe + 4] != b'PE\0\0' or
                        struct.unpack_from('<H', data, pe + 4)[0] != 0x14c or
                        struct.unpack_from('<H', data, pe + 24)[0] != 0x10b):
                    raise AssertionError('Native test executable must be x86 PE32')
            cls.executables[name] = executable

    def check_case(self, case, expected):
        for name, executable in self.executables.items():
            with self.subTest(path=name):
                self.assertIn(expected, run([executable, case]).stdout)

    def test_all_widths_boundaries_clipping_and_maximal_runs(self):
        self.check_case('boundaries', 'PASS: all 320 widths')

    def test_invalid_inputs_preserve_state_and_outputs(self):
        self.check_case('invalid', 'PASS: invalid dimensions/state/pointers/coordinates')

    def test_random_interleavings_against_independent_pixel_oracle(self):
        self.check_case('random', 'PASS: 128000 random reset/mark/find interleavings')

    def test_complete_exact_painter_reversal_and_transparent_zero(self):
        self.check_case('painter', 'PASS: 256 exact painter sequence reversals')


class CoverageSpansDosTests(unittest.TestCase):
    @unittest.skipUnless(imported.COMPILER.is_file(), 'existing DJGPP unavailable')
    def test_i486_djgpp_compile_link_and_kernel_instruction_contract(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-coverage-dos-') as directory:
            directory = Path(directory)
            for name, defines in (('bsf', []), ('portable', ['-DDOS_COVERAGE_PORTABLE=1'])):
                objects = []
                for source in (PLATFORM / 'coverage_spans.c', ROOT / 'tests/coverage_spans_test.c'):
                    obj = directory / f'{source.stem}-{name}.o'
                    run([imported.COMPILER, *imported.CFLAGS, '-Werror', *defines,
                         '-I', PLATFORM, '-c', source, '-o', obj])
                    self.assertEqual(imported.coff_object(obj.read_bytes())['format'], 'coff-i386')
                    objects.append(obj)
                run([imported.COMPILER, *objects, '-o', directory / f'coverage-{name}.exe'])
                # Check actual target kernel code, not the native runtime/test harness.
                objdump = imported.COMPILER.with_name(imported.COMPILER.name.replace('gcc', 'objdump'))
                disassembly = run([objdump, '-dr', objects[0]]).stdout.lower()
                if name == 'bsf':
                    self.assertRegex(disassembly, r'\bbsf\b')
                self.assertNotRegex(disassembly, r'\b(tzcnt|lzcnt|popcnt|cmov[a-z]*|[i]?div)\b')
                nm = imported.COMPILER.with_name(imported.COMPILER.name.replace('gcc', 'nm'))
                symbols = imported.parse_nm(run([nm, '-g', objects[0]]).stdout)
                for function in ('reset', 'find_run', 'mark_span', 'mark_pixel'):
                    self.assertEqual(symbols.get('_dos_coverage_' + function), 'T')
                self.assertNotIn('U', symbols.values())


if __name__ == '__main__':
    unittest.main()
