"""Bounded host startup/ISO checks; synthetic signatures are not guest boot proof."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from game_media import embedded_boot, startup
from iso_image import build_iso


BLOCK = 2048
FLOPPY_BYTES = 1440 * 1024
DESCRIPTOR = 17 * BLOCK


class StartupTests(unittest.TestCase):
    def test_normal_startup_is_direct_game_with_deferred_audio(self):
        expected = (b'@ECHO OFF\r\nSET PATH=A:\\\r\n'
                    b'CTMOUSE /S14 /R11 /W /Y\r\nCWSDPMI -p -s-\r\n'
                    b'DOSCRAFT\r\nECHO DOScraft exited. Audio is deferred.\r\n')
        self.assertEqual(startup(), expected)
        self.assertEqual(startup(False), expected)

    def test_smoke_startup_guards_reload_after_failed_save(self):
        expected = (b'@ECHO OFF\r\nSET PATH=A:\\\r\n'
                    b'CTMOUSE /S14 /R11 /W /Y\r\nCWSDPMI -p -s-\r\n'
                    b'DOSCRAFT /SMOKE\r\nIF ERRORLEVEL 1 GOTO DONE\r\n'
                    b'DOSCRAFT /SMOKELOAD\r\n:DONE\r\n'
                    b'ECHO Game test finished.\r\n')
        self.assertEqual(startup(True), expected)


class EmbeddedBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Deliberately no DOS bootstrap, FAT filesystem or private game payload.
        boot = bytearray(bytes(range(256)) * (FLOPPY_BYTES // 256))
        boot[510:512] = b'\x55\xaa'
        cls.boot = bytes(boot)
        cls.iso = build_iso(cls.boot, {'README.TXT': b'synthetic host test'})
        cls.catalog = struct.unpack_from('<I', cls.iso, DESCRIPTOR + 71)[0] * BLOCK
        cls.boot_offset = struct.unpack_from('<I', cls.iso, cls.catalog + 40)[0] * BLOCK

    def fix_checksum(self, image):
        """Keep header mutations checksum-valid to exercise their own checks."""
        struct.pack_into('<H', image, self.catalog + 28, 0)
        checksum = (-sum(struct.unpack_from('<16H', image, self.catalog))) & 0xffff
        struct.pack_into('<H', image, self.catalog + 28, checksum)
        self.assertEqual(sum(struct.unpack_from('<16H', image, self.catalog)) & 0xffff, 0)

    def test_generated_iso_reads_back_entire_floppy(self):
        result = embedded_boot(self.iso)
        self.assertIsInstance(result, bytes)
        self.assertEqual(len(result), FLOPPY_BYTES)
        self.assertEqual(result, self.boot)

    def test_bytes_like_images_read_back_without_mutation(self):
        for image in (bytearray(self.iso), memoryview(self.iso)):
            with self.subTest(image_type=type(image).__name__):
                self.assertEqual(bytes(embedded_boot(image)), self.boot)
                self.assertEqual(bytes(image), self.iso)

    def test_non_buffer_objects_are_rejected(self):
        for image in (None, 1, object()):
            with self.subTest(image_type=type(image).__name__), self.assertRaises(TypeError):
                embedded_boot(image)

    def test_catalog_pointer_follows_multi_sector_directory(self):
        # 46 short files force a second root-directory sector; no fixed catalog LBA.
        image = build_iso(self.boot, {f'F{i:04}.BIN': b'x' for i in range(46)})
        catalog = struct.unpack_from('<I', image, DESCRIPTOR + 71)[0] * BLOCK
        self.assertGreater(catalog, self.catalog)
        self.assertEqual(embedded_boot(image), self.boot)

    def test_empty_data_directory_reads_back(self):
        self.assertEqual(embedded_boot(build_iso(self.boot, {})), self.boot)

    def test_descriptor_type_identifier_and_version_are_rejected(self):
        for offset in (0, 1, 5, 6):
            bad = bytearray(self.iso)
            bad[DESCRIPTOR + offset] ^= 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, 'Invalid ISO boot descriptor'):
                embedded_boot(bad)

    def test_el_torito_identifier_and_padding_are_rejected(self):
        for offset in (7, 28, 38):
            bad = bytearray(self.iso)
            bad[DESCRIPTOR + offset] ^= 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, 'Not an El Torito'):
                embedded_boot(bad)

    def test_catalog_header_platform_reserved_bytes_and_keys_are_rejected(self):
        for offset in (0, 1, 2, 3, 30, 31):
            bad = bytearray(self.iso)
            bad[self.catalog + offset] ^= 1
            self.fix_checksum(bad)
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, 'Invalid x86 floppy boot catalog'):
                embedded_boot(bad)

    def test_catalog_checksum_rejects_corrupt_identifier_or_checksum_word(self):
        for offset in (4, 27, 28, 29):
            bad = bytearray(self.iso)
            bad[self.catalog + offset] ^= 1
            self.assertNotEqual(sum(struct.unpack_from('<16H', bad, self.catalog)) & 0xffff, 0)
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, 'Invalid x86 floppy boot catalog'):
                embedded_boot(bad)

    def test_nonbootable_catalog_entries_are_rejected(self):
        for indicator in (0, 1, 0x89, 0xff):
            bad = bytearray(self.iso)
            bad[self.catalog + 32] = indicator
            with self.subTest(indicator=indicator), self.assertRaisesRegex(ValueError, 'Invalid x86 floppy boot catalog'):
                embedded_boot(bad)

    def test_other_emulation_media_types_are_rejected(self):
        # Only 2 (1.44-MiB floppy) is the generated launcher's media contract.
        for media in (0, 1, 3, 4, 0xff):
            bad = bytearray(self.iso)
            bad[self.catalog + 33] = media
            with self.subTest(media=media), self.assertRaisesRegex(ValueError, 'Invalid x86 floppy boot catalog'):
                embedded_boot(bad)

    def test_initial_sector_count_must_be_one(self):
        for count in (0, 2, 256, 0xffff):
            bad = bytearray(self.iso)
            struct.pack_into('<H', bad, self.catalog + 38, count)
            with self.subTest(count=count), self.assertRaisesRegex(ValueError, 'Invalid x86 floppy boot catalog'):
                embedded_boot(bad)

    def test_invalid_catalog_extents_are_rejected(self):
        for lba in (0, 17, len(self.iso) // BLOCK - 1, len(self.iso) // BLOCK, 0xffffffff):
            bad = bytearray(self.iso)
            struct.pack_into('<I', bad, DESCRIPTOR + 71, lba)
            with self.subTest(lba=lba), self.assertRaisesRegex(ValueError, 'Invalid x86 floppy boot catalog'):
                embedded_boot(bad)

    def test_invalid_floppy_extents_are_rejected(self):
        for lba in (0, self.catalog // BLOCK, self.boot_offset // BLOCK + 1,
                    len(self.iso) // BLOCK, 0xffffffff):
            bad = bytearray(self.iso)
            struct.pack_into('<I', bad, self.catalog + 40, lba)
            with self.subTest(lba=lba), self.assertRaisesRegex(ValueError, 'Invalid embedded floppy'):
                embedded_boot(bad)

    def test_missing_floppy_signature_is_rejected(self):
        for signature in (b'\x00\x00', b'\xaa\x55', b'\x55\x00', b'\x00\xaa'):
            bad = bytearray(self.iso)
            bad[self.boot_offset + 510:self.boot_offset + 512] = signature
            with self.subTest(signature=signature), self.assertRaisesRegex(ValueError, 'Invalid embedded floppy'):
                embedded_boot(bad)

    def test_unaligned_truncations_are_rejected(self):
        for length in (1, DESCRIPTOR + 74, self.catalog + BLOCK - 1,
                       self.boot_offset + FLOPPY_BYTES - 1, len(self.iso) - 1):
            with self.subTest(length=length), self.assertRaisesRegex(ValueError, 'Invalid ISO boot descriptor'):
                embedded_boot(self.iso[:length])

    def test_aligned_truncations_are_rejected_at_descriptor_catalog_or_floppy(self):
        cases = ((0, 'Invalid ISO boot descriptor'),
                 (DESCRIPTOR, 'Invalid ISO boot descriptor'),
                 (DESCRIPTOR + BLOCK, 'Invalid ISO descriptor terminator'),
                 (self.catalog, 'Invalid x86 floppy boot catalog'),
                 (self.catalog + BLOCK, 'Invalid embedded floppy'),
                 (self.boot_offset + FLOPPY_BYTES - BLOCK, 'Invalid embedded floppy'))
        for length, message in cases:
            with self.subTest(length=length), self.assertRaisesRegex(ValueError, message):
                embedded_boot(self.iso[:length])

    def test_primary_and_terminator_signatures(self):
        for offset in (16*BLOCK,16*BLOCK+6,18*BLOCK,18*BLOCK+1):
            bad=bytearray(self.iso); bad[offset]^=1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                embedded_boot(bad)

    def test_default_load_segment_system_type_reserved_byte(self):
        for offset in (34,35,36,37):
            bad=bytearray(self.iso); bad[self.catalog+offset]=1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError,'Invalid x86 floppy boot catalog'):
                embedded_boot(bad)


if __name__ == '__main__':
    unittest.main()
