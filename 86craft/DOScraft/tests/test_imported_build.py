"""Mechanical source/COFF contract tests; no guest or gameplay claims."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_imported as imported


class AdaptationTests(unittest.TestCase):
    def test_selection_is_explicit_and_excludes_hardware(self):
        self.assertEqual(set(imported.PORTABLE_C), {name + '.c' for name in (
            'blocks', 'fmath', 'font', 'game', 'inventory', 'mobs', 'physics',
            'player', 'raster', 'render', 'textures', 'ui', 'world', 'libc')})
        self.assertFalse(set(imported.SELECTED) & set(imported.EXCLUDED))
        self.assertIn('trap.S', imported.SELECTED)
        self.assertEqual(set(imported.INCLUDES),
                         {'column_store.inc', 'column_hdd.inc', 'column_cache.inc'})
        for flag in ('-march=i486', '-mtune=i486', '-O2', '-fno-builtin',
                     '-mno-80387', '-fno-tree-loop-distribute-patterns'):
            self.assertIn(flag, imported.CFLAGS)
        self.assertFalse(any(flag.startswith('-D') or flag in ('-O3', '-ffast-math', '-flto')
                             for flag in imported.CFLAGS))

    def test_unselected_future_code_is_not_automatically_imported(self):
        self.assertNotIn('new_game.c', imported.SELECTED)
        data = b'/* preserve fixed point and assembly */\r\nint x;\r\n'
        for name in ('fmath.h', 'world.c', 'raster.c', 'render.c'):
            self.assertEqual(imported.adapt_source(name, data), (data, []))

    def test_libc_only_signatures_change(self):
        source = '\n'.join(before + '\n{ /* unchanged */ return 0; }'
                           for before, _ in imported.LIBC_SIGNATURES) + '\n'
        result, recipe = imported.adapt_source('libc.c', source.encode())
        expected = source
        for before, after in imported.LIBC_SIGNATURES:
            expected = expected.replace(before + '\n', after + '\n')
        self.assertEqual(result.decode(), expected)
        self.assertEqual(len(recipe), 5)
        with self.assertRaises(ValueError):
            imported.adapt_source('libc.c', source.replace('int strlen', 'long strlen').encode())
        with self.assertRaises(ValueError):
            imported.replace_once('x x', 'x', 'y', [])

    def test_coff_metadata_removal_is_bounded(self):
        source = ''.join(f'\tTRAP {symbol},1,0,0\n' for symbol in imported.TRAP_SYMBOLS)
        source += '\tmovl g_recip14(,%ecx,4),%ecx\nlocal: ret\n'
        source += '.type trap_opaque1,@function\n.size trap_opaque1,.-trap_opaque1\n'
        source += '\t.section .note.GNU-stack,"",@progbits\n'
        result, recipe = imported.adapt_source('trap.S', source.encode())
        replay = source
        for step in recipe:
            self.assertEqual(replay.count(step['before']), step['count'])
            replay = replay.replace(step['before'], step['after'])
        self.assertEqual(result.decode(), replay)
        self.assertIn('local: ret\n', replay)
        self.assertIn('_g_recip14(,%ecx,4)', replay)
        self.assertNotIn('.type', replay)
        self.assertNotIn('.size', replay)
        for extra in ('.type new_function,@function\n',
                      '.section .note.other,"",@progbits\n'):
            with self.assertRaises(ValueError):
                imported.adapt_source('trap.S', (source + extra).encode())

    def test_coff_rejects_executables_elf_and_truncation(self):
        data = struct.pack('<HHIIIHH', 0x14c, 3, 0, 0, 0, 0, 0)
        self.assertEqual(imported.coff_object(data)['format'], 'coff-i386')
        for bad in (b'\x7fELF', data[:19], struct.pack('<HHIIIHH', 0x14c, 3, 0, 0, 0, 0, 2),
                    struct.pack('<HHIIIHH', 0x8664, 3, 0, 0, 0, 0, 0)):
            with self.assertRaises(ValueError):
                imported.coff_object(bad)

    def test_symbol_check_requires_both_sides_of_trap_abi(self):
        groups = {'trap.o': {'_' + name: 'T' for name in imported.TRAP_SYMBOLS},
                  'raster.o': {'_' + name: 'U' for name in imported.TRAP_SYMBOLS},
                  'libc.o': {'_' + name: 'T' for name in (
                      'memset', 'memcpy', 'memmove', 'memcmp', 'strlen',
                      'itoa_dec', 'rnd', 'rnd_seed', 'rnd_range', 'hash3')},
                  'tables.o': {'_g_sinTab': 'R', '_g_atanTab': 'R'}}
        groups['trap.o']['_g_recip14'] = 'U'
        groups['raster.o']['_g_recip14'] = 'B'
        imported.verify_symbols(groups)
        groups['tables.o'] = {'_g_sinTab': 'T', '_g_atanTab': 'T'}
        imported.verify_symbols(groups)
        groups['raster.o']['_trap_opaque1'] = 'T'
        with self.assertRaises(ValueError):
            imported.verify_symbols(groups)


@unittest.skipUnless((imported.VENDOR / 'MANIFEST.json').is_file(), 'Vendor not materialized')
class VendorTests(unittest.TestCase):
    def test_staging_preserves_bodies_and_records_replayable_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            report = imported.stage_sources(imported.VENDOR, output)
            for entry in report['files']:
                original = (imported.VENDOR / entry['source']).read_bytes()
                staged = (output / entry['staged']).read_bytes()
                self.assertEqual(imported.sha256(original), entry['source_sha256'])
                self.assertEqual(imported.sha256(staged), entry['staged_sha256'])
                self.assertEqual(imported.sha256(imported.canonical_json(entry['transformations'])),
                                 entry['transformations_sha256'])
                replay = original
                for step in entry['transformations']:
                    before, after = step['before'].encode(), step['after'].encode()
                    self.assertEqual(replay.count(before), step['count'])
                    replay = replay.replace(before, after)
                self.assertEqual(replay, staged)
                self.assertEqual((imported.VENDOR / entry['source']).read_bytes(), original)
            for name in imported.PORTABLE_C:
                if name != 'libc.c':
                    self.assertEqual((output / 'src' / name).read_bytes(),
                                     (imported.VENDOR / 'src' / name).read_bytes())
            common = (output / 'src/common.h').read_text()
            self.assertIn('#include "dos_compat.h"', common)
            self.assertIn('char *itoa_dec', common)
            self.assertIn('u32 rnd(void)', common)
            self.assertNotIn('int strlen', common)
            self.assertNotIn('hw.h', common)
            self.assertNotIn('hw.h', (output / 'src/hdd.h').read_text())
            self.assertFalse((output / 'src/hw.h').exists())
            self.assertIn('#include <string.h>', imported.DOS_COMPAT)
            self.assertNotIn('outb', imported.DOS_COMPAT)
            self.assertIn('offsetof(Trap,pixels)==68', imported.abi_probe().decode())

    def test_mismatched_vendor_is_rejected_before_staging(self):
        provenance = json.loads((imported.VENDOR / 'MANIFEST.json').read_bytes())
        next(entry for entry in provenance['files'] if entry['path'] == 'src/world.c')['sha256'] = '0' * 64
        real_read = Path.read_bytes

        def changed_read(path):
            if path == imported.VENDOR / 'MANIFEST.json':
                return json.dumps(provenance).encode()
            return real_read(path)

        with tempfile.TemporaryDirectory() as directory, patch.object(Path, 'read_bytes', changed_read):
            output = Path(directory)
            with self.assertRaisesRegex(ValueError, 'Vendor checksum mismatch'):
                imported.stage_sources(imported.VENDOR, output)
            self.assertFalse((output / 'src').exists())

    def test_failed_build_replaces_stale_success_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / 'build.json').write_text('{"status":"verified_objects"}')
            with self.assertRaises(FileNotFoundError):
                imported.build(output=output, compiler=output / 'missing-gcc.exe')
            report = json.loads((output / 'build.json').read_bytes())
            self.assertEqual(report['status'], 'failed')
            self.assertEqual(report['objects'], [])
            self.assertFalse(report['linked'])
            self.assertFalse(report['executable'])
            self.assertFalse(report['playable'])


if __name__ == '__main__':
    unittest.main()
