"""Native parity and actual DJGPP i486 code audit; no guest/host speed claim."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DOS = ROOT / 'src/platform/dos'
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
DJGPP_BIN = ROOT / 'build/deps/djgpp/djgpp/bin'
DJGPP = DJGPP_BIN / 'i586-pc-msdosdjgpp-gcc.exe'
OBJDUMP = DJGPP_BIN / 'i586-pc-msdosdjgpp-objdump.exe'
NM = DJGPP_BIN / 'i586-pc-msdosdjgpp-nm.exe'
I486_FLAGS = (
    '-march=i486', '-mtune=i486', '-O2', '-ffreestanding', '-fno-pic',
    '-fno-pie', '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
    '-fno-builtin', '-mno-80387', '-mno-mmx', '-mno-sse', '-mgeneral-regs-only',
    '-fno-tree-loop-distribute-patterns', '-Wall', '-Wextra', '-Werror',
    '-std=gnu99', '-fstrict-aliasing',
)


def run(arguments, timeout=60):
    result = subprocess.run([str(arg) for arg in arguments], capture_output=True,
                            text=True, timeout=timeout)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {arguments}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result.stdout


def instructions(disassembly):
    result = []
    for line in disassembly.splitlines():
        match = re.match(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)'
                         r'([a-z][a-z0-9]*)\s*(.*)$', line)
        if match:
            address, encoding, mnemonic, operands = match.groups()
            result.append((int(address, 16), len(encoding.split()), mnemonic, operands))
    if not result:
        raise AssertionError(f'No instructions found:\n{disassembly}')
    return result


def inner_loop(decoded):
    loops = []
    for index, (address, size, mnemonic, operands) in enumerate(decoded):
        if mnemonic not in ('jne', 'jnz'):
            continue
        target = re.match(r'([0-9a-f]+)\s', operands)
        if target and int(target[1], 16) < address:
            start = int(target[1], 16)
            body = [inst for inst in decoded[:index + 1] if start <= inst[0] <= address]
            loops.append(body)
    if not loops:
        raise AssertionError('Expected a backward inner-loop branch')
    return min(loops, key=len)


def loop_cost(decoded):
    body = inner_loop(decoded)
    source_loads = destination_stores = stack_accesses = 0
    for _, _, mnemonic, operands in body:
        if re.search(r'\(%e(?:sp|bp)\)', operands):
            stack_accesses += 1
        if not mnemonic.startswith('mov'):
            continue
        left, _, right = operands.partition(',')
        if '(' in left and not re.search(r'%e(?:sp|bp)', left):
            source_loads += 1
        if '(' in right and not re.search(r'%e(?:sp|bp)', right):
            destination_stores += 1
    return {
        'instructions_per_16_pixels': len(body),
        'inner_loop_bytes': sum(inst[1] for inst in body),
        'source_loads': source_loads,
        'destination_stores': destination_stores,
        'stack_accesses': stack_accesses,
        'rotates': sum(inst[2] == 'ror' for inst in body),
        'exchanges': sum(inst[2] == 'xchg' for inst in body),
    }


class VgaPackWordsNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix='doscraft-vga-words-native-')
        cls.addClassCleanup(temporary.cleanup)
        cls.executables = {}
        if sys.platform == 'win32' and ZIG.is_file():
            compilers = {
                'i386': [ZIG, 'cc', '-target', 'x86-windows-gnu', '-march=i486'],
                'x86_64': [ZIG, 'cc', '-target', 'x86_64-windows-gnu'],
            }
        else:
            native = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
            if not native:
                raise unittest.SkipTest('Native C compiler unavailable')
            compilers = {'native': [native]}
        for architecture, compiler in compilers.items():
            for portable in (False, True):
                name = architecture + ('-portable' if portable else '-default')
                executable = Path(temporary.name) / (name + '.exe')
                defines = ['-DDOS_VGA_PACK_WORDS_PORTABLE'] if portable else []
                run([*compiler, '-std=c99', '-O2', '-fno-builtin', '-fstrict-aliasing',
                     '-Wall', '-Wextra', '-Werror', *defines, '-I', DOS,
                     ROOT / 'tests/vga_pack_words_test.c', DOS / 'vga_pack_words.c',
                     DOS / 'vga_pack_all.c', DOS / 'vga_pack.c', '-o', executable])
                cls.executables[name] = executable

    def native(self, mode, expected):
        for name, executable in self.executables.items():
            with self.subTest(variant=name):
                output = run([executable, mode])
                self.assertIn(expected, output)
                print(f'{name}: {output.strip()}')

    def test_reference_original_and_plane_parity_all_alignments(self):
        self.native('valid', 'PASS: 6475 frames; independent reference')

    def test_all_invalid_pitches_capacities_and_nulls(self):
        self.native('invalid', 'PASS: all 320 short pitches, all 76800 short capacities')

    def test_exact_extents_and_unreadable_invalid_buffers(self):
        self.native('bounds', 'PASS: 17 exact source/destination extents')


@unittest.skipUnless(DJGPP.is_file() and OBJDUMP.is_file() and NM.is_file(),
                     'Actual DJGPP compiler/binutils unavailable')
class VgaPackWordsDjgppTests(unittest.TestCase):
    def test_actual_i486_objects_no_helpers_and_kernel_cost(self):
        self.assertEqual(run([DJGPP, '-dumpmachine']).strip(), 'i586-pc-msdosdjgpp')
        version = run([DJGPP, '-dumpfullversion']).strip()
        print(f'DJGPP GCC {version}, -march=i486 -mtune=i486 -O2, production integer flags')
        # An explicit i486 instruction allowlist also rejects hidden calls,
        # division, conditional moves, FPU, SIMD and BMI/newer instructions.
        allowed = {
            'push', 'pop', 'mov', 'movb', 'movl', 'movzbl', 'movzwl', 'lea',
            'add', 'sub', 'and', 'or', 'xor', 'test', 'cmp', 'shr', 'shl',
            'ror', 'xchg', 'je', 'jne', 'jbe', 'ja', 'jb', 'jae', 'jmp',
            'inc', 'dec', 'ret', 'nop',
        }
        allowed.update(base + suffix for base in (
            'mov', 'add', 'sub', 'and', 'or', 'xor', 'test', 'cmp',
            'shr', 'shl', 'ror', 'xchg', 'inc', 'dec',
        ) for suffix in ('b', 'w', 'l'))
        with tempfile.TemporaryDirectory(prefix='doscraft-vga-words-djgpp-') as directory:
            costs = {}
            for name, source, defines in (
                ('words', 'vga_pack_words.c', []),
                ('portable', 'vga_pack_words.c', ['-DDOS_VGA_PACK_WORDS_PORTABLE']),
                ('original', 'vga_pack_all.c', []),
            ):
                obj = Path(directory) / (name + '.o')
                run([DJGPP, *I486_FLAGS, *defines, '-c', DOS / source, '-o', obj])
                self.assertEqual(run([NM, '-u', obj]).strip(), '', 'Unexpected helper reference')
                disassembly = run([OBJDUMP, '-dr', obj])
                self.assertIn('file format coff-go32', disassembly)
                decoded = instructions(disassembly)
                for address, _, mnemonic, operands in decoded:
                    self.assertIn(mnemonic, allowed, f'{name} at {address:x}: {mnemonic} {operands}')
                    self.assertNotRegex(operands, r'%(?:[xyz]mm|mm|st|cr|dr)\d?')
                cost = loop_cost(decoded)
                costs[name] = cost
                print(f'{name}: {cost}')
                if name == 'words':
                    self.assertEqual(cost['source_loads'], 4)
                    self.assertEqual(cost['destination_stores'], 4)
                    self.assertEqual(cost['rotates'], 12)
                    self.assertEqual(cost['exchanges'], 6)
                    self.assertLessEqual(cost['instructions_per_16_pixels'], 31)
                    self.assertLessEqual(cost['stack_accesses'], 1,
                                         'Unexpected data spills in transpose loop')
                    body = inner_loop(decoded)
                    # Each source/destination access must remain a full dword.
                    for _, _, mnemonic, operands in body:
                        if mnemonic == 'mov' and '(' in operands:
                            left, _, right = operands.partition(',')
                            register = right if '(' in left else left
                            self.assertRegex(register, r'^%e(?:ax|bx|cx|dx)$')
                elif name == 'original':
                    self.assertEqual(cost['source_loads'], 16)
                    self.assertEqual(cost['destination_stores'], 16)
            for name in ('words', 'original'):
                count = costs[name]['instructions_per_16_pixels'] * 20 * 240
                print(f'{name}: {count} inner-loop instructions/frame, excluding row/setup overhead')
            print('Static instruction/access counts only; guest timing required for adoption.')


if __name__ == '__main__':
    unittest.main()
