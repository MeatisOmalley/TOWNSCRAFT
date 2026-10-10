"""Native x86 first-opaque backend against the actual pinned painter/assembly."""
from pathlib import Path
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
import dos_sources

EXPORTS = (
    'g_recip14', 'g_statPixels', 'g_fastFlat', 'g_statOcclusionTiles',
    'raster_set_target', 'raster_width', 'raster_height', 'raster_poly',
    'raster_flat_poly', 'raster_pixel', 'raster_line', 'raster_fill_rows',
    'raster_finish', 'raster_occlusion_reset', 'raster_occluder', 'raster_occluded',
)
TRAPS = imported.TRAP_SYMBOLS


def run(command):
    command = list(map(str, command))
    result = subprocess.run(command, capture_output=True, text=True,
                            errors='replace', timeout=120)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result


def stage(folder):
    imported.stage_sources(imported.VENDOR, folder / 'imported')
    src = folder / 'imported/src'
    original = (src / 'raster.c').read_bytes()
    adapted, recipe = dos_sources.adapt('raster.c', original, front_to_back=True)
    if not recipe or adapted == original:
        raise AssertionError('Expected the explicit F2B raster overlay')
    replay = original
    for step in recipe:
        before, after = step['before'].encode(), step['after'].encode()
        if replay.count(before) != step['count']:
            raise AssertionError('F2B raster recipe does not replay exactly')
        replay = replay.replace(before, after)
    if replay != adapted:
        raise AssertionError('F2B raster recipe differs from candidate')
    candidate = folder / 'candidate.c'
    candidate.write_bytes(adapted)
    # raster.c has its own anonymous Trap typedef. Native macro routing would
    # collide with f2b_raster.h's equivalent, separately tagged wrapper types.
    # Hide only those unused header declarations in this translation unit;
    # its actual trap declarations still become the eight __wrap_* symbols.
    shim = folder / 'candidate_declarations.h'
    shim.write_text('\n'.join([
        *(f'#define __wrap_{name} unused_header_{name}' for name in TRAPS),
        '#include "f2b_raster.h"',
        *(f'#undef __wrap_{name}' for name in TRAPS),
        '',
    ]))
    return src, candidate, shim


@unittest.skipUnless(sys.platform == 'win32' and ZIG.is_file(),
                     'Pinned native x86 Windows Zig harness unavailable')
class F2BRasterNativeTests(unittest.TestCase):
    defines = ()
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix='doscraft-f2b-raster-')
        cls.addClassCleanup(temporary.cleanup)
        folder = Path(temporary.name)
        src, candidate, shim = stage(folder)
        flags = [ZIG, 'cc', '-target', 'x86-windows-gnu', '-mcpu=i486',
                 '-O2', '-std=gnu99', '-UNDEBUG', '-fno-builtin', '-Wall',
                 '-Wextra', '-Wno-unused-function', '-I', src, '-I', PLATFORM]
        flags.extend(cls.defines)
        run([*flags, '-c', src / 'abi_check.c', '-o', folder / 'abi_check.o'])
        objects = []
        sources = [
            (src / 'raster.c', 'reference',
             [f'-D{name}=reference_{name}' for name in EXPORTS]),
            (candidate, 'candidate', ['-include', shim,
             *(f'-D{name}=__wrap_{name}' for name in TRAPS)]),
            (src / 'trap.S', 'trap', []),
            *((PLATFORM / name, Path(name).stem, []) for name in
              ('f2b_raster.c', 'f2b_span.S', 'f2b_texel.c', 'coverage_spans.c',
               'f2b_capture.c')),
        ]
        for source, name, defines in sources:
            obj = folder / (name + '.o')
            run([*flags, *defines, '-Werror', '-c', source, '-o', obj])
            imported.coff_object(obj.read_bytes())
            objects.append(obj)
        cls.executable = folder / 'test.exe'
        run([*flags, '-Werror', ROOT / 'tests/f2b_raster_test.c', *objects,
             '-o', cls.executable])
        data = cls.executable.read_bytes()
        if data[:2] != b'MZ':
            raise AssertionError('Expected native PE executable')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if (data[pe:pe + 4] != b'PE\0\0' or
                struct.unpack_from('<H', data, pe + 4)[0] != 0x14c or
                struct.unpack_from('<H', data, pe + 24)[0] != 0x10b):
            raise AssertionError('Expected actual x86 PE32 executable')

    def check_mode(self, mode, expected):
        result = run([self.executable, mode])
        self.assertIn(expected, result.stdout)
        print(result.stdout.strip())

    def test_reverse_replay_whole_images_against_original_painter(self):
        self.check_mode('scenes', 'PASS: 128 original-painter scenes')

    def test_all_wrappers_active_sparse_gaps_and_inactive_trap_mutations(self):
        self.check_mode('traps', 'PASS: 4096 actual-trap cases')

    def test_actual_opaque_assembly_single_odd_even_phase_and_gaps(self):
        self.check_mode('phase', 'PASS: 360 actual-assembly phase cases')

    def test_invalid_targets_clipping_opaque_zero_hidden_rect_and_full(self):
        self.check_mode('bounds', 'PASS: target/begin bounds')


class F2BFastRasterNativeTests(F2BRasterNativeTests):
    defines = ('-DDOS_F2B_FAST_COVERAGE',)


class F2BRasterDosTests(unittest.TestCase):
    defines = ()
    @unittest.skipUnless(imported.COMPILER.is_file(), 'Existing DJGPP unavailable')
    def test_i486_djgpp_objects_and_original_assembly_wrapper_symbols(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-f2b-raster-dos-') as directory:
            folder = Path(directory)
            src, candidate, shim = stage(folder)
            compiler = imported.COMPILER
            self.assertEqual(run([compiler, '-dumpmachine']).stdout.strip(),
                             'i586-pc-msdosdjgpp')
            nm = compiler.with_name(compiler.name.replace('gcc', 'nm'))
            objects = {}
            sources = [
                (src / 'raster.c', 'reference',
                 [f'-D{name}=reference_{name}' for name in EXPORTS]),
                (candidate, 'candidate', ['-include', shim,
                 *(f'-D{name}=__wrap_{name}' for name in TRAPS)]),
                (src / 'trap.S', 'trap', []),
                *((PLATFORM / name, Path(name).stem, []) for name in
                  ('f2b_raster.c', 'f2b_span.S', 'f2b_texel.c', 'coverage_spans.c',
                   'f2b_capture.c')),
                (ROOT / 'tests/f2b_raster_test.c', 'harness', []),
            ]
            for source, name, defines in sources:
                obj = folder / (name + '.o')
                flags = imported.ASFLAGS if source.suffix == '.S' else imported.CFLAGS
                run([compiler, *flags, *defines, *self.defines, '-UNDEBUG', '-Werror', '-I', src,
                     '-I', PLATFORM, '-c', source, '-o', obj])
                self.assertEqual(imported.coff_object(obj.read_bytes())['format'],
                                 'coff-i386')
                objects[name] = imported.parse_nm(run([nm, '-g', obj]).stdout)
            for name in EXPORTS:
                self.assertIn(objects['reference'].get('_reference_' + name),
                              ('T', 'B', 'C', 'D'))
                self.assertNotIn('_' + name, objects['reference'])
            for name in TRAPS:
                self.assertEqual(objects['candidate'].get('___wrap_' + name), 'U')
                self.assertEqual(objects['f2b_raster'].get('___wrap_' + name), 'T')
                self.assertEqual(objects['f2b_raster'].get('___real_' + name), 'U')
                self.assertEqual(objects['harness'].get('___real_' + name), 'T')
                self.assertEqual(objects['trap'].get('_' + name), 'T')
                self.assertNotIn('_' + name, objects['candidate'])
            self.assertEqual(objects['f2b_raster'].get('_g_recip14'), 'U')
            if self.defines:
                self.assertNotIn('_dos_coverage_find_run', objects['f2b_raster'])
                self.assertNotIn('_dos_coverage_mark_span', objects['f2b_raster'])
            self.assertIn(objects['candidate'].get('_g_recip14'), ('B', 'C', 'D'))
            self.assertEqual(objects['f2b_span'].get('_dos_f2b_opaque_span'), 'T')
            print('PASS: 9 DJGPP i486 COFF objects, original exports, real trap '
                  'forwarders, wrapper routing and candidate reciprocal ABI')


class F2BFastRasterDosTests(F2BRasterDosTests):
    defines = ('-DDOS_F2B_FAST_COVERAGE',)


if __name__ == '__main__':
    unittest.main()
