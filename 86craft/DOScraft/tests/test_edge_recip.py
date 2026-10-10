"""Exact edge quotient proofs: original x86 IDIV, real raster/traps, DJGPP core."""
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
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported
import dos_sources

EXPORTS = (
    'g_recip14', 'g_statPixels', 'g_fastFlat', 'g_statOcclusionTiles',
    'raster_occlusion_reset', 'raster_occluded', 'raster_set_target',
    'raster_width', 'raster_height', 'raster_poly', 'raster_flat_poly',
    'raster_occluder', 'raster_pixel', 'raster_line', 'raster_fill_rows',
    'raster_finish',
)


def run(command):
    command = list(map(str, command))
    result = subprocess.run(command, capture_output=True, text=True,
                            errors='replace', timeout=120)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result


def replay(data, recipe):
    for step in recipe:
        before, after = step['before'].encode(), step['after'].encode()
        if step['operation'] != 'replace' or data.count(before) != step['count']:
            raise AssertionError('Edge reciprocal recipe does not replay exactly')
        data = data.replace(before, after)
    return data


def stage(folder):
    imported.stage_sources(imported.VENDOR, folder / 'imported')
    src = folder / 'imported/src'
    original = (src / 'raster.c').read_bytes()
    candidate, recipe = dos_sources.adapt('raster.c', original, edge_recip=True)
    if not recipe or candidate == original or replay(original, recipe) != candidate:
        raise AssertionError('Expected exact, replayable parent edge_recip overlay')
    candidate_path = src / 'candidate.c'
    candidate_path.write_bytes(candidate)
    return src, candidate_path


class EdgeRecipRecipeTests(unittest.TestCase):
    def test_parent_overlay_is_optional_exact_and_replayable(self):
        for name in imported.SELECTED:
            original = (imported.VENDOR / 'src' / name).read_bytes()
            self.assertEqual(dos_sources.adapt(name, original), (original, []))
            candidate, recipe = dos_sources.adapt(name, original, edge_recip=True)
            self.assertEqual(replay(original, recipe), candidate)
            if name == 'raster.c':
                text = candidate.decode().replace('\r\n', '\n')
                self.assertEqual(text.count('#include "edge_recip.h"'), 1)
                self.assertEqual(text.count('dos_edge_recip_init();'), 1)
                self.assertEqual(text.count('dos_edge_recip_divshift('), 3)
                before = original.decode().replace('\r\n', '\n')
                expected = before.replace('#include "fmath.h"\n',
                                          '#include "fmath.h"\n#include "edge_recip.h"\n')
                expected = expected.replace('\trscale=scale;\n',
                                            '\trscale=scale;\n\tdos_edge_recip_init();\n')
                for call in ('divshift(b->x-a->x,dy,16)',
                             'divshift(du,dy,4)', 'divshift(dv,dy,4)'):
                    self.assertEqual(before.count(call), 1)
                    expected = expected.replace(call, 'dos_edge_recip_' + call)
                self.assertEqual(text, expected, 'Overlay changed more than include/init/3 divisions')
            else:
                self.assertEqual((candidate, recipe), (original, []))
        with self.assertRaises(ValueError):
            dos_sources.adapt('raster.c', b'unexpected source', edge_recip=True)


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(),
                     'Pinned native x86 Windows Zig harness unavailable')
class EdgeRecipNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix='doscraft-edge-recip-native-')
        cls.addClassCleanup(temporary.cleanup)
        folder = Path(temporary.name)
        src, candidate = stage(folder)
        flags = [ZIG, 'cc', '-target', 'x86-windows-gnu', '-mcpu=i486',
                 '-O2', '-std=gnu99', '-fno-builtin', '-Wall', '-Wextra',
                 '-Werror', '-Wno-unused-function', '-mno-80387', '-mno-mmx',
                 '-mno-sse', '-I', src, '-I', PLATFORM]
        run([*flags, '-c', src / 'abi_check.c', '-o', folder / 'abi_check.o'])
        sources = (
            (src / 'raster.c', 'reference',
             [f'-D{name}=reference_{name}' for name in EXPORTS]),
            (candidate, 'candidate', []),
            (src / 'trap.S', 'trap', []),
            (PLATFORM / 'edge_recip.c', 'edge_recip', []),
        )
        objects = []
        for source, name, defines in sources:
            obj = folder / (name + '.o')
            run([*flags, *defines, '-c', source, '-o', obj])
            imported.coff_object(obj.read_bytes())
            objects.append(obj)
        cls.executable = folder / 'test.exe'
        run([*flags, ROOT / 'tests/edge_recip_test.c', *objects, '-o', cls.executable])
        cls.parity_executable = folder / 'parity.exe'
        run([*flags, ROOT / 'tests/raster_parity_test.c', *objects,
             '-o', cls.parity_executable])
        data = cls.executable.read_bytes()
        if data[:2] != b'MZ':
            raise AssertionError('Expected native PE executable')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if (data[pe:pe + 4] != b'PE\0\0' or
                struct.unpack_from('<H', data, pe + 4)[0] != 0x14c or
                struct.unpack_from('<H', data, pe + 24)[0] != 0x10b):
            raise AssertionError('Expected actual x86 PE32 executable')

    def test_original_idiv_all_divisors_signs_boundaries_random_and_fallbacks(self):
        output = run([self.executable, 'division']).stdout
        self.assertIn('exact original-IDIV divisions', output)
        self.assertIn('all 8192 divisors, signed boundaries', output)
        print(output.strip())

    def test_reused_original_raster_trap_pixels_and_occlusion(self):
        output = run([self.parity_executable]).stdout
        self.assertIn('PASS: 24000 polygons', output)
        print(output.strip())

    def test_real_raster_trap_pixels_clipping_negative_uv_and_frame_guards(self):
        output = run([self.executable, 'guards']).stdout
        self.assertIn('PASS: 8192 convex polygons', output)
        print(output.strip())


def function_body(disassembly, name):
    match = re.search(rf'^\s*[0-9a-f]+ <_{re.escape(name)}>:\n(.*?)'
                      r'(?=^\s*[0-9a-f]+ <[^>]+>:\n|\Z)',
                      disassembly, re.MULTILINE | re.DOTALL)
    if not match:
        raise AssertionError(f'Missing disassembled function {name}')
    return match.group(1)


def instructions(disassembly):
    result = re.findall(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\b',
                        disassembly, re.MULTILINE)
    if not result:
        raise AssertionError('Expected real object instructions')
    return result


class EdgeRecipDosTests(unittest.TestCase):
    @unittest.skipUnless(imported.COMPILER.is_file(), 'Existing DJGPP unavailable')
    def test_i486_djgpp_fast_core_mull_correction_no_idiv_calls_helpers_or_fpu(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-edge-recip-djgpp-') as directory:
            folder = Path(directory)
            src, candidate = stage(folder)
            compiler = imported.COMPILER
            self.assertEqual(run([compiler, '-dumpmachine']).stdout.strip(),
                             'i586-pc-msdosdjgpp')
            nm = compiler.with_name(compiler.name.replace('gcc', 'nm'))
            objdump = compiler.with_name(compiler.name.replace('gcc', 'objdump'))
            flags = [*imported.CFLAGS, '-Werror', '-I', src, '-I', PLATFORM]
            sources = (
                (PLATFORM / 'edge_recip.c', 'init', []),
                (ROOT / 'tests/edge_recip_test.c', 'audit', ['-DEDGE_RECIP_AUDIT']),
                (candidate, 'candidate', []),
                (src / 'trap.S', 'trap', []),
                (src / 'abi_check.c', 'abi_check', []),
            )
            for source, name, defines in sources:
                obj = folder / (name + '.o')
                run([compiler, *flags, *defines, '-c', source, '-o', obj])
                self.assertEqual(imported.coff_object(obj.read_bytes())['format'], 'coff-i386')
            candidate_symbols = imported.parse_nm(run([nm, folder / 'candidate.o']).stdout)
            self.assertFalse(any('dos_edge_recip_divshift' in name or
                                 'dos_edge_recip_quotient' in name
                                 for name in candidate_symbols),
                             'Real raster must inline every fast division')
            audit = folder / 'audit.o'
            symbols = imported.parse_nm(run([nm, '-g', audit]).stdout)
            undefined = {name for name, kind in symbols.items() if kind == 'U'}
            self.assertEqual(undefined, {'_g_dos_edge_recip'}, 'Unexpected runtime/helper dependency')
            disassembly = run([objdump, '-dr', '--no-show-raw-insn', audit]).stdout.lower()
            for shift in (4, 16):
                fast = function_body(disassembly, f'audit_fast{shift}')
                ops = instructions(fast)
                self.assertTrue(any(op.startswith('mul') for op in ops), 'Missing MULL high32')
                self.assertTrue(any(op.startswith('imul') for op in ops), 'Missing correction product')
                for op in ops:
                    self.assertFalse(op.startswith(('div', 'idiv', 'call', 'f')), op)
                full = function_body(disassembly, f'audit_full{shift}')
                full_ops = instructions(full)
                self.assertTrue(any(op.startswith('idiv') for op in full_ops), 'Missing original fallback')
                self.assertTrue(any(op.startswith('mul') for op in full_ops), 'Fast core was not inlined')
            for op in instructions(disassembly):
                self.assertFalse(op.startswith(('call', 'f')), op)
            self.assertNotRegex(disassembly, r'%[xyz]mm\d|%st\b|'
                                r'\b(tzcnt|lzcnt|popcnt|cmov[a-z]*|mulq|imulq|addq)\b')
            init_obj = folder / 'init.o'
            init_symbols = imported.parse_nm(run([nm, '-g', init_obj]).stdout)
            self.assertEqual(init_symbols.get('_dos_edge_recip_init'), 'T')
            self.assertIn(init_symbols.get('_g_dos_edge_recip'), ('B', 'C'))
            self.assertNotIn('U', init_symbols.values(), 'Init must have no runtime helpers')
            init_disassembly = run([objdump, '-dr', '--no-show-raw-insn', init_obj]).stdout.lower()
            for op in instructions(init_disassembly):
                self.assertFalse(op.startswith(('call', 'f')), op)
            self.assertNotRegex(init_disassembly, r'%[xyz]mm\d|%st\b')
            print('PASS: 5 DJGPP i486 COFF objects; inlined MULL/correction/sign, '
                  'no fast IDIV/calls/helpers/FPU/SIMD; original fallback retained')


if __name__ == '__main__':
    unittest.main()
