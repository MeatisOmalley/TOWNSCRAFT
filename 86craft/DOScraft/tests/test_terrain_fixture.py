"""Pinned temporary terrain layout tests; no DOS or emulator required."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import terrain_fixture


class TerrainFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = terrain_fixture.terrain_image()

    def test_exact_marker_and_little_endian_fields(self):
        self.assertIsInstance(self.image, bytes)
        # The C literal has 16 characters; its NUL is outside the compared marker.
        self.assertEqual(self.image[:16], b'TSC-TERRAIN-TEMP')
        self.assertEqual(self.image[16:28],
                         b'\x01\x00\x00\x00\x00\x01\x00\x00\x18\x00\x00\x00')
        self.assertEqual(struct.unpack_from('<III', self.image, 16), (1, 256, 24))

    def test_header_extensions_and_payload_are_zero(self):
        self.assertEqual(self.image[28:512], bytes(512 - 28))
        self.assertEqual(self.image[512:], bytes(256 * 2 * 24 * 512))

    def test_exact_bounded_capacity(self):
        self.assertEqual(len(self.image), 6291968)
        self.assertEqual(len(self.image) // 512, 1 + 256 * 2 * 24)
        self.assertEqual(terrain_fixture.SECTOR_BYTES, 512)
        self.assertEqual(terrain_fixture.COLUMNS, 256)
        self.assertEqual(terrain_fixture.SECTORS_PER_COLUMN, 24)
        self.assertEqual(terrain_fixture.SLOTS_PER_COLUMN, 2)
        self.assertEqual(terrain_fixture.SLOT_BYTES, 12288)
        self.assertEqual(terrain_fixture.IMAGE_BYTES, len(self.image))

    def test_alternating_slots_are_disjoint_and_within_capacity(self):
        _, columns, sectors = struct.unpack_from('<III', self.image, 16)
        slot_bytes = sectors * 512
        previous_end = 512
        for column in range(columns):
            # Towns terrain_capture: first, then first + COLUMN_CELLS, then first.
            first = (1 + column * sectors * 2) * 512
            alternate = first + slot_bytes
            self.assertEqual(alternate - first, 12288)
            for start in (first, alternate):
                with self.subTest(column=column, start=start):
                    end = start + slot_bytes
                    self.assertEqual(start % 512, 0)
                    self.assertEqual(start, previous_end)
                    self.assertLessEqual(end, len(self.image))
                    previous_end = end
        self.assertEqual(previous_end, len(self.image))

    def test_temporary_backing_is_not_claimed_as_a_save_format(self):
        self.assertIn('not a save format', terrain_fixture.__doc__)
        self.assertIn('not a save format', terrain_fixture.terrain_image.__doc__)
        self.assertIn('Record ownership lives in RAM', terrain_fixture.__doc__)

    def test_repeat_calls_return_the_same_blank_bytes(self):
        self.assertEqual(terrain_fixture.terrain_image(), self.image)


if __name__ == '__main__':
    unittest.main()
