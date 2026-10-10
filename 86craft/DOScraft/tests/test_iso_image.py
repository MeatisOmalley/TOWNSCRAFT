"""Independent host-side ISO9660/El Torito parsing; no mounts or emulator."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from fat_image import FatImage, populate
from iso_image import build_iso


BLOCK = 2048
FLOPPY_BYTES = 1474560
DATE = bytes((126, 10, 9, 0, 0, 0, 0))


class IsoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        boot = bytearray(512)
        boot[:11] = b'\xeb\x3c\x90DOSCRAFT'
        struct.pack_into('<HBHBHHBH', boot, 11, 512, 1, 1, 2, 224, 2880, 0xf0, 9)
        boot[510:512] = b'\x55\xaa'
        # A real FAT12 image exercises the existing builder without claiming
        # this synthetic boot sector contains a DOS bootstrap implementation.
        cls.floppy = bytes(populate(boot, [('KERNEL.SYS', bytes(range(256)) * 8)]))

    def both(self, data, offset, width):
        little = int.from_bytes(data[offset:offset + width], 'little')
        big = int.from_bytes(data[offset + width:offset + 2 * width], 'big')
        self.assertEqual(little, big, 'ISO little/big-endian pair differs')
        return little

    def record(self, data, offset=0):
        length = data[offset]
        record = data[offset:offset + length]
        self.assertGreaterEqual(length, 34)
        self.assertEqual(len(record), length)
        identifier_size = record[32]
        self.assertEqual(length, 33 + identifier_size + (1 if identifier_size % 2 == 0 else 0))
        self.assertEqual(record[1], 0)  # extended attribute length
        self.assertEqual(record[18:25], DATE)
        self.assertEqual(record[26:28], bytes(2))  # no interleaving
        self.assertEqual(self.both(record, 28, 2), 1)
        if identifier_size % 2 == 0:
            self.assertEqual(record[-1], 0)
        return {
            'extent': self.both(record, 2, 4),
            'size': self.both(record, 10, 4),
            'flags': record[25],
            'name': record[33:33 + identifier_size],
            'length': length,
        }

    def inspect(self, iso):
        """Parse on-disk fields only; do not reuse writer layout or helpers."""
        self.assertIsInstance(iso, bytes)
        self.assertEqual(len(iso) % BLOCK, 0)
        self.assertEqual(iso[:16 * BLOCK], bytes(16 * BLOCK))
        descriptors = []
        for lba in range(16, len(iso) // BLOCK):
            descriptor = iso[lba * BLOCK:(lba + 1) * BLOCK]
            self.assertEqual(descriptor[1:7], b'CD001\x01')
            descriptors.append(descriptor)
            if descriptor[0] == 255:
                break
        self.assertEqual([d[0] for d in descriptors], [1, 0, 255])
        pvd, brvd, terminator = descriptors
        self.assertEqual(terminator[7:], bytes(BLOCK - 7))
        self.assertEqual(pvd[8:40], b'DOSCRAFT'.ljust(32, b' '))
        self.assertEqual(self.both(pvd, 80, 4), len(iso) // BLOCK)
        self.assertEqual(self.both(pvd, 120, 2), 1)
        self.assertEqual(self.both(pvd, 124, 2), 1)
        self.assertEqual(self.both(pvd, 128, 2), BLOCK)
        self.assertEqual(pvd[813:830], b'2026100900000000\x00')
        self.assertEqual(pvd[830:847], pvd[813:830])
        self.assertEqual(pvd[847:864], b'0' * 16 + b'\x00')
        self.assertEqual(pvd[864:881], pvd[813:830])
        self.assertEqual(pvd[881:883], b'\x01\x00')
        root = self.record(pvd, 156)
        self.assertEqual(root['name'], b'\x00')
        self.assertEqual(root['flags'], 2)
        self.assertEqual(root['size'] % BLOCK, 0)
        self.assertGreaterEqual(root['size'], BLOCK)

        path_size = self.both(pvd, 132, 4)
        self.assertEqual(path_size, 10)
        path_lbas = []
        for offset, byte_order in ((140, 'little'), (148, 'big')):
            lba = int.from_bytes(pvd[offset:offset + 4], byte_order)
            path_lbas.append(lba)
            table = iso[lba * BLOCK:(lba + 1) * BLOCK]
            self.assertEqual(len(table), BLOCK)
            self.assertEqual(table[:2], b'\x01\x00')
            self.assertEqual(int.from_bytes(table[2:6], byte_order), root['extent'])
            self.assertEqual(int.from_bytes(table[6:8], byte_order), 1)
            self.assertEqual(table[8:10], b'\x00\x00')
            self.assertEqual(table[path_size:], bytes(BLOCK - path_size))
        self.assertEqual(pvd[144:148], bytes(4))
        self.assertEqual(pvd[152:156], bytes(4))

        self.assertEqual(brvd[7:39], b'EL TORITO SPECIFICATION'.ljust(32, b'\x00'))
        self.assertEqual(brvd[39:71], bytes(32))
        self.assertEqual(brvd[75:], bytes(BLOCK - 75))
        catalog_lba = int.from_bytes(brvd[71:75], 'little')
        catalog = iso[catalog_lba * BLOCK:(catalog_lba + 1) * BLOCK]
        self.assertEqual(len(catalog), BLOCK)
        self.assertEqual(catalog[:4], b'\x01\x00\x00\x00')
        self.assertEqual(catalog[30:32], b'\x55\xaa')
        self.assertEqual(sum(struct.unpack('<16H', catalog[:32])) & 0xffff, 0)
        self.assertEqual(catalog[32:38], b'\x88\x02\x00\x00\x00\x00')
        self.assertEqual(int.from_bytes(catalog[38:40], 'little'), 1)
        self.assertEqual(catalog[44:], bytes(BLOCK - 44))
        boot_lba = int.from_bytes(catalog[40:44], 'little')
        embedded_boot = iso[boot_lba * BLOCK:boot_lba * BLOCK + FLOPPY_BYTES]
        self.assertEqual(embedded_boot, self.floppy)
        self.assertEqual(embedded_boot[510:512], b'\x55\xaa')

        start = root['extent'] * BLOCK
        directory = iso[start:start + root['size']]
        self.assertEqual(len(directory), root['size'])
        records = []
        offset = 0
        while offset < len(directory):
            if directory[offset] == 0:
                end = min((offset // BLOCK + 1) * BLOCK, len(directory))
                self.assertEqual(directory[offset:end], bytes(end - offset))
                offset = end
                continue
            record = self.record(directory, offset)
            self.assertLessEqual(offset % BLOCK + record['length'], BLOCK)
            records.append(record)
            offset += record['length']
        self.assertGreaterEqual(len(records), 2)
        for record, name in zip(records[:2], (b'\x00', b'\x01')):
            self.assertEqual(record['name'], name)
            self.assertEqual(record['extent'], root['extent'])
            self.assertEqual(record['size'], root['size'])
            self.assertEqual(record['flags'], 2)

        # Check distinct, bounded extents for descriptors, paths, directory,
        # catalog, full emulated floppy and every file (including empty files).
        spans = [(0, len(descriptors) + 16)]
        spans.extend((lba, lba + 1) for lba in path_lbas)
        spans.extend([(root['extent'], root['extent'] + root['size'] // BLOCK),
                      (catalog_lba, catalog_lba + 1),
                      (boot_lba, boot_lba + FLOPPY_BYTES // BLOCK)])
        files = {}
        for record in records[2:]:
            name = record['name'].decode('ascii')
            self.assertEqual(record['flags'], 0)
            self.assertNotIn(name, files)
            extent, size = record['extent'], record['size']
            sectors = max(1, (size + BLOCK - 1) // BLOCK)
            spans.append((extent, extent + sectors))
            contents = iso[extent * BLOCK:extent * BLOCK + size]
            self.assertEqual(len(contents), size)
            files[name] = contents
            padding = iso[extent * BLOCK + size:(extent + sectors) * BLOCK]
            self.assertEqual(padding, bytes(sectors * BLOCK - size))
        def sort_key(name):
            stem, _, suffix = name.partition('.')
            extension, _, version = suffix.partition(';')
            self.assertEqual(version, '1')
            return stem, extension

        self.assertEqual(list(files), sorted(files, key=sort_key))
        spans.sort()
        for begin, end in spans:
            self.assertGreaterEqual(begin, 0)
            self.assertLess(begin, end)
            self.assertLessEqual(end, len(iso) // BLOCK)
        for left, right in zip(spans, spans[1:]):
            self.assertLessEqual(left[1], right[0], 'ISO extents overlap')
        self.assertEqual(spans[-1][1], len(iso) // BLOCK)
        return pvd, root, records, files

    def test_boot_catalog_descriptors_and_all_files(self):
        inputs = {'DOScraft.EXE': bytes(range(256)) * 300,
                  'README.TXT': b'Boot A:, run CD program, save to HDD.\r\n',
                  'EMPTY.DAT': b'', 'CONFIG': b'plain root filename'}
        pvd, _, _, files = self.inspect(build_iso(self.floppy, inputs))
        self.assertEqual(pvd[40:72], b'DOSCRAFT'.ljust(32, b' '))
        self.assertEqual(files, {'DOSCRAFT.EXE;1': inputs['DOScraft.EXE'],
                                 'README.TXT;1': inputs['README.TXT'],
                                 'EMPTY.DAT;1': b'', 'CONFIG.;1': inputs['CONFIG']})
        self.assertEqual(FatImage(self.floppy).read('KERNEL.SYS'), bytes(range(256)) * 8)

    def test_empty_root(self):
        _, root, records, files = self.inspect(build_iso(self.floppy, {}))
        self.assertEqual(root['size'], BLOCK)
        self.assertEqual(len(records), 2)
        self.assertEqual(files, {})

    def test_iso_filename_then_extension_order(self):
        inputs = {'AA': b'4', 'A.X': b'3', 'A.BIN': b'2', 'A': b'1'}
        _, _, _, files = self.inspect(build_iso(self.floppy, inputs))
        self.assertEqual(list(files), ['A.;1', 'A.BIN;1', 'A.X;1', 'AA.;1'])

    def test_file_sector_boundaries(self):
        sizes = (0, 1, 511, 512, 513, 2047, 2048, 2049, 4095, 4096, 4097, 65536)
        inputs = {f'F{i}.BIN': bytes((j % 251 for j in range(size)))
                  for i, size in enumerate(sizes)}
        _, _, _, files = self.inspect(build_iso(self.floppy, inputs))
        self.assertEqual(files, {name + ';1': payload for name, payload in inputs.items()})

    def test_directory_sector_boundary_and_multi_sector_root(self):
        # Dot entries use 68 bytes; each F0000.BIN;1 uses 44. 45 fit exactly.
        for count, expected_sectors in ((44, 1), (45, 1), (46, 2), (150, 4)):
            with self.subTest(count=count):
                inputs = {f'F{i:04}.BIN': struct.pack('<I', i) for i in range(count)}
                _, root, records, files = self.inspect(build_iso(self.floppy, inputs))
                self.assertEqual(root['size'], expected_sectors * BLOCK)
                self.assertEqual(len(records), count + 2)
                self.assertEqual(files, {name + ';1': data for name, data in inputs.items()})

    def test_directory_identifier_padding_and_maximum_names(self):
        inputs = {'A': b'1', 'AB': b'2', 'ABCDEFGH.XYZ': b'3', '01234567._09': b'4'}
        pvd, _, records, files = self.inspect(build_iso(self.floppy, inputs, 'A' * 32))
        self.assertEqual(pvd[40:72], b'A' * 32)
        self.assertEqual(files, {'A.;1': b'1', 'AB.;1': b'2',
                                 'ABCDEFGH.XYZ;1': b'3', '01234567._09;1': b'4'})
        self.assertEqual({len(r['name']) % 2 for r in records[2:]}, {0, 1})

    def test_deterministic_order_case_and_bytes_like_inputs(self):
        first = build_iso(self.floppy, {'b.txt': b'B', 'a.exe': b'A'})
        floppy = bytearray(self.floppy)
        payload = bytearray(b'A')
        second = build_iso(floppy, {'A.EXE': payload, 'B.TXT': memoryview(b'B')})
        third = build_iso(memoryview(self.floppy), {'a.exe': b'A', 'b.txt': b'B'})
        self.assertEqual(first, second)
        self.assertEqual(first, third)
        self.assertEqual(floppy, self.floppy)
        self.assertEqual(payload, b'A')
        self.inspect(first)

    def test_invalid_floppy_sizes_and_signatures(self):
        for size in (0, 512, FLOPPY_BYTES - 1, FLOPPY_BYTES + 1, 1228800, 2949120):
            with self.subTest(size=size), self.assertRaises(ValueError):
                build_iso(bytes(size), {})
        for signature in (b'\x00\x00', b'\xaa\x55', b'\x55\x00', b'\x00\xaa'):
            bad = bytearray(self.floppy)
            bad[510:512] = signature
            with self.subTest(signature=signature), self.assertRaises(ValueError):
                build_iso(bad, {})

    def test_invalid_names_and_duplicates(self):
        names = ('', '.', '..', 'A.', '.TXT', 'TOOLONG99.EXE', 'A.LONG',
                 'A.B.C', 'DIR/A.EXE', 'DIR\\A.EXE', '/A.EXE', 'C:A.EXE',
                 'A B.TXT', 'A;1', 'A-B.TXT', 'A\x00.BIN', 'A\n', 'é.TXT', 'ß.TXT')
        for name in names:
            with self.subTest(name=name), self.assertRaises(ValueError):
                build_iso(self.floppy, {name: b'x'})
        for inputs in ({'A': b'1', 'a': b'2'}, {'DOSCRAFT.EXE': b'1', 'DOScraft.EXE': b'2'}):
            with self.assertRaises(ValueError):
                build_iso(self.floppy, inputs)

    def test_invalid_input_types(self):
        for value in (None, 1474560, 'floppy', [0] * 512):
            with self.subTest(boot_type=type(value)), self.assertRaises(TypeError):
                build_iso(value, {})
        for value in (None, [], [('A', b'x')], b'', 'files'):
            with self.subTest(files_type=type(value)), self.assertRaises(TypeError):
                build_iso(self.floppy, value)
        for value in (None, 0, True, 'text', [], {'data': b'x'}):
            with self.subTest(payload_type=type(value)), self.assertRaises(TypeError):
                build_iso(self.floppy, {'A': value})
        for value in (None, 1, b'A'):
            with self.subTest(name_type=type(value)), self.assertRaises(TypeError):
                build_iso(self.floppy, {value: b'x'})
        for value in (None, 1, b'DOSCRAFT'):
            with self.subTest(volume_type=type(value)), self.assertRaises(TypeError):
                build_iso(self.floppy, {}, value)

    def test_invalid_volume_ids(self):
        for value in ('', 'A' * 33, 'doscraft', 'DOS CRAFT', 'DOS-CRAFT', 'é', 'A\x00'):
            with self.subTest(volume=value), self.assertRaises(ValueError):
                build_iso(self.floppy, {}, value)

    def test_independent_parser_detects_corrupt_iso_fields(self):
        valid = build_iso(self.floppy, {'A.BIN': b'payload'})
        _, root, _, _ = self.inspect(valid)
        brvd = valid[17 * BLOCK:18 * BLOCK]
        catalog = int.from_bytes(brvd[71:75], 'little') * BLOCK
        offsets = (16 * BLOCK + 84,  # mismatched big-endian volume size
                   16 * BLOCK + 128,  # wrong logical block size
                   catalog + 28,  # validation checksum
                   catalog + 32,  # boot indicator
                   catalog + 33,  # floppy media type
                   catalog + 40,  # boot image RBA
                   root['extent'] * BLOCK + 6)  # root record endian pair
        for offset in offsets:
            bad = bytearray(valid)
            bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(AssertionError):
                self.inspect(bytes(bad))
        with self.assertRaises(AssertionError):
            self.inspect(valid[:-1])


if __name__ == '__main__':
    unittest.main()
