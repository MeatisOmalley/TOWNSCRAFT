"""Texture phase scaffold: pinned trap.S parity, no game/emulator integration."""
from pathlib import Path
import re
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


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True,
                            errors='replace', timeout=60)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(),
                     'Pinned native x86 Windows Zig harness unavailable')
class F2BTexelNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix='doscraft-f2b-texel-')
        cls.addClassCleanup(temporary.cleanup)
        folder = Path(temporary.name)
        # Official checksum-validated staging; no instruction/body overlays.
        imported.stage_sources(imported.VENDOR, folder / 'imported')
        src = folder / 'imported/src'
        flags = [ZIG, 'cc', '-target', 'x86-windows-gnu', '-mcpu=i486',
                 '-O2', '-std=gnu99', '-fno-builtin', '-Wall', '-Wextra',
                 '-Wno-unused-function', '-I', src, '-I', PLATFORM]
        run([*flags, '-c', src / 'abi_check.c', '-o', folder / 'abi_check.o'])
        objects = []
        for source in (src / 'raster.c', src / 'trap.S', PLATFORM / 'f2b_texel.c'):
            obj = folder / (source.stem + '.o')
            run([*flags, '-c', source, '-o', obj])
            imported.coff_object(obj.read_bytes())
            objects.append(obj)
        cls.executable = folder / 'test.exe'
        run([*flags, '-Werror', ROOT / 'tests/f2b_texel_test.c', *objects,
             '-o', cls.executable])
        data = cls.executable.read_bytes()
        if data[:2] != b'MZ':
            raise AssertionError('Expected native PE executable')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if (data[pe:pe + 4] != b'PE\0\0' or
                struct.unpack_from('<H', data, pe + 4)[0] != 0x14c or
                struct.unpack_from('<H', data, pe + 24)[0] != 0x10b):
            raise AssertionError('Expected actual x86 PE32 executable')
        assembly = folder / 'f2b_texel.s'
        run([*flags, '-Werror', '-ffreestanding', '-mno-80387', '-mno-sse',
             '-mno-mmx', '-S', PLATFORM / 'f2b_texel.c', '-o', assembly])
        cls.assembly = assembly.read_text()

    def test_sparse_gaps_against_all_real_trap_variants(self):
        result = run([self.executable, 'parity'])
        self.assertIn('PASS: 20480 pinned trap rows', result.stdout)
        print(result.stdout.strip())

    def test_every_jump_against_repeated_add_adc(self):
        result = run([self.executable, 'carry'])
        self.assertIn('PASS: 0..320 jumps', result.stdout)
        print(result.stdout.strip())

    def test_invalid_calls_preserve_all_state_bytes(self):
        result = run([self.executable, 'invalid'])
        self.assertIn('PASS: invalid init/jump', result.stdout)
        print(result.stdout.strip())

    def test_core_has_no_calls_divides_fpu_or_wide_math(self):
        # Inspect emitted i486 core instructions, excluding all test/runtime code.
        for instruction in re.findall(r'^\s*([a-z][a-z0-9]*)\s',
                                      self.assembly, re.MULTILINE):
            self.assertFalse(instruction.startswith(('call', 'div', 'idiv', 'f')),
                             instruction)
        self.assertNotRegex(self.assembly, r'%[xyz]mm\d|\b(?:mulq|imulq|addq)\b')


class F2BTexelDosTests(unittest.TestCase):
    @unittest.skipUnless(imported.COMPILER.is_file(), 'existing DJGPP unavailable')
    def test_i486_djgpp_object_symbols_and_instruction_contract(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-f2b-texel-dos-') as directory:
            obj = Path(directory) / 'f2b_texel.o'
            self.assertEqual(run([imported.COMPILER, '-dumpmachine']).stdout.strip(),
                             'i586-pc-msdosdjgpp')
            run([imported.COMPILER, *imported.CFLAGS, '-Werror', '-I', PLATFORM,
                 '-c', PLATFORM / 'f2b_texel.c', '-o', obj])
            self.assertEqual(imported.coff_object(obj.read_bytes())['format'], 'coff-i386')
            nm = imported.COMPILER.with_name(imported.COMPILER.name.replace('gcc', 'nm'))
            symbols = imported.parse_nm(run([nm, '-g', obj]).stdout)
            for function in ('init', 'jump'):
                self.assertEqual(symbols.get('_dos_f2b_texel_' + function), 'T')
            self.assertNotIn('U', symbols.values(), 'Core must have no external helpers')
            objdump = imported.COMPILER.with_name(
                imported.COMPILER.name.replace('gcc', 'objdump'))
            disassembly = run([objdump, '-dr', '--no-show-raw-insn', obj]).stdout.lower()
            instructions = re.findall(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\b',
                                      disassembly, re.MULTILINE)
            self.assertTrue(instructions, 'Expected actual DJGPP core disassembly')
            for instruction in instructions:
                self.assertFalse(instruction.startswith(('call', 'div', 'idiv', 'f')),
                                 instruction)
            self.assertNotRegex(disassembly,
                                r'\b(tzcnt|lzcnt|popcnt|cmov[a-z]*|mulq|imulq|addq)\b|'
                                r'%[xyz]mm\d|%st\b')
            print('PASS: DJGPP i486 COFF core; init/jump exports, no undefined helpers, '
                  'calls, divides, FPU/SIMD or wide math')


if __name__ == '__main__':
    unittest.main()
