"""Host-side media safety and file-chain tests; no guest/emulator needed."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from fat_image import FatImage, populate, scratch_hdd, short_name


class MediaTests(unittest.TestCase):
    def floppy_boot(self):
        b = bytearray(512)
        b[:11] = b'\xeb\x3c\x90DOSCRAFT'
        struct.pack_into('<HBHBHHBH', b, 11, 512, 1, 1, 2, 224, 2880, 0xf0, 9)
        b[510:512] = b'\x55\xaa'
        return b

    def test_floppy_roundtrip(self):
        payload = bytes(range(256)) * 20
        image = populate(self.floppy_boot(), [('TEST.BIN', payload), ('EMPTY.TXT', b'')])
        fs = FatImage(image)
        self.assertEqual(fs.bits, 12)
        self.assertEqual(fs.read('test.bin'), payload)
        self.assertEqual(fs.read('EMPTY.TXT'), b'')
        with self.assertRaises(FileNotFoundError): fs.read('MISSING.TXT')

    def test_fat16_disk(self):
        image = scratch_hdd()
        start, size = struct.unpack_from('<II', image, 454)
        fs = FatImage(image, start * 512)
        self.assertEqual(fs.bits, 16)
        self.assertEqual(fs.total, size)
        self.assertEqual(fs.directory(), [])
        boot = image[start * 512:start * 512 + 512]
        payload = bytes(range(256)) * 130
        volume = populate(boot, [('TEST.BIN', payload)])
        self.assertEqual(FatImage(volume).read('TEST.BIN'), payload)

    def test_invalid_names_and_full_disk(self):
        for name in ('../BAD', 'TOOLONGNAME.EXE', 'A.XYZQ', 'A B.TXT'):
            with self.assertRaises(ValueError): short_name(name)
        with self.assertRaises(ValueError): populate(self.floppy_boot(), [('A', b'1'), ('a', b'2')])
        with self.assertRaises(ValueError): populate(self.floppy_boot(), [('FULL', bytes(1474560))])

    def test_cycle_and_truncation_rejected(self):
        image = populate(self.floppy_boot(), [('TEST.BIN', bytes(1024))])
        # FAT12 entry 2 -> itself. Preserve entry 3's nibble.
        word = struct.unpack_from('<H', image, 515)[0]
        struct.pack_into('<H', image, 515, (word & 0xf000) | 2)
        with self.assertRaises(ValueError): FatImage(image).read('TEST.BIN')
        with self.assertRaises(ValueError): FatImage(image[:-512])


if __name__ == '__main__': unittest.main()
