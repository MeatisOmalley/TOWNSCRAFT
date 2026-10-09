"""Direct-port heap parity and DJGPP linkage; no DOS guest execution claim."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ZIG = ROOT.parents[1] / 'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported


def reference_source(directory):
    """Use the actual pinned allocator body, changing only exported names."""
    provenance = json.loads((imported.VENDOR / 'MANIFEST.json').read_bytes())
    data = (imported.VENDOR / 'src/sys.c').read_bytes()
    entry = next(item for item in provenance['files'] if item['path'] == 'src/sys.c')
    if (provenance['source_commit'] != imported.PINNED_COMMIT or
            imported.sha256(data) != entry['sha256'] or len(data) != entry['size']):
        raise AssertionError('Pinned Towns sys.c provenance mismatch')
    source = data.decode('utf-8').replace('\r\n', '\n')
    body = source.split('/* Heap */\n', 1)[1].split('\nvoid sys_init(void)', 1)[0]
    renames = '\n'.join(f'#define heap_{name} reference_{name}' for name in (
        'alloc_low', 'alloc_high', 'low_free', 'high_free', 'high_mark', 'high_rewind'))
    result = (directory / 'test_heap_reference.c')
    result.write_text(renames + '\n#include <stdint.h>\n#include "heap.h"\n'
                      'typedef unsigned int u32;\ntypedef unsigned char u8;\n'
                      + body + '\n'
                      'void reference_init(u32 start,u32 end,u32 high_end) {\n'
                      'lowPtr=start; lowEnd=end; highPtr=0x100000; highEnd=high_end;\n'
                      '}\n')
    return result


def run(command, timeout=120):
    result = subprocess.run(list(map(str, command)), capture_output=True,
                            text=True, errors='replace', timeout=timeout)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {command}\n'
                             f'{result.stdout}\n{result.stderr}')
    return result


class HeapTests(unittest.TestCase):
    @unittest.skipUnless(ZIG.is_file(), 'existing native Zig toolchain unavailable')
    def test_native_pinned_parity_and_lifecycle(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-heap-native-') as directory:
            directory = Path(directory)
            reference = reference_source(directory)
            executable = directory / 'test_heap.exe'
            run([ZIG, 'cc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                 # Original Towns pointers are 32-bit virtual addresses in the
                 # reference only, never dereferenced on the 64-bit test host.
                 '-Wno-int-to-pointer-cast', '-I', ROOT / 'src/platform/dos',
                 ROOT / 'src/platform/dos/heap.c', ROOT / 'tests/heap_test.c',
                 reference, '-o', executable])
            result = run([executable], timeout=10)
            self.assertIn('PASS: pinned Towns parity, 80000 trace steps', result.stdout)

    @unittest.skipUnless(imported.COMPILER.is_file(), 'existing DJGPP unavailable')
    def test_dos_compile_link_and_imported_declarations(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-heap-dos-') as directory:
            directory = Path(directory)
            # Staging to private test storage verifies pinned source hashes and
            # exercises the real generated common.h/dos_compat.h include path.
            imported.stage_sources(imported.VENDOR, directory / 'imported')
            staged = directory / 'imported/src'
            reference = reference_source(directory)
            objects = []
            for source in (ROOT / 'src/platform/dos/heap.c',
                           ROOT / 'tests/heap_test.c', reference):
                obj = directory / (source.stem + '.o')
                run([imported.COMPILER, *imported.CFLAGS, '-Werror',
                     '-I', ROOT / 'src/platform/dos', '-I', staged,
                     '-include', staged / 'sys.h', '-c', source, '-o', obj])
                self.assertEqual(imported.coff_object(obj.read_bytes())['format'], 'coff-i386')
                objects.append(obj)
            run([imported.COMPILER, *objects, '-o', directory / 'test_heap.exe'])
            nm = imported.COMPILER.with_name(imported.COMPILER.name.replace('gcc', 'nm'))
            symbols = imported.parse_nm(run([nm, '-g', objects[0]]).stdout)
            for name in ('dos_heap_init', 'dos_heap_shutdown', 'heap_alloc_low',
                         'heap_alloc_high', 'heap_low_free', 'heap_high_free',
                         'heap_high_mark', 'heap_high_rewind'):
                self.assertEqual(symbols.get('_' + name), 'T')
            self.assertEqual(symbols.get('_fatal'), 'U')
            self.assertNotIn('_sys_init', symbols)
            self.assertNotIn('_g_ramMB', symbols)


if __name__ == '__main__':
    unittest.main()
