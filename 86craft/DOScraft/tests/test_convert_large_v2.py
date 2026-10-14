"""V2 1024-world medium: codec, tile layout and (when baked) exact V1 equivalence."""
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import convert_large_v2 as v2


class ConvertTests(unittest.TestCase):
    def test_codec_matches_column_encode_and_round_trips(self):
        column = bytes([11] * 3 + [1] * 300 + [0] * 45) * 16 + bytes(range(256)) * 10
        packed = v2.rle(column)
        self.assertEqual(packed[:4], bytes((3, 11, 255, 1)))
        self.assertEqual(v2.decode(v2.encode(column), len(column)), column)
        noisy = bytes((i * 37) & 255 for i in range(12288))
        self.assertEqual(v2.encode(noisy), noisy)  # raw when RLE is not smaller
        self.assertEqual(v2.decode(noisy, 12288), noisy)

    def test_tiles_hold_16x16_cells(self):
        row_major = bytearray(512 * 512 * 2)
        for z in range(512):
            for x in range(512):
                row_major[(z * 512 + x) * 2:(z * 512 + x) * 2 + 2] = bytes((x & 255, z & 255))
        tiles = v2.far_tiles(row_major)
        for x, z in ((0, 0), (17, 3), (511, 511), (300, 129)):
            at = ((z >> 4) * 32 + (x >> 4)) * 512 + ((z & 15) * 16 + (x & 15)) * 2
            self.assertEqual(tiles[at:at + 2], bytes((x & 255, z & 255)))

    @unittest.skipUnless((ROOT / 'build/world/seed4242-v2.img').exists() and
                         (ROOT / 'build/world/seed4242-far.img').exists(), 'baked worlds absent')
    def test_baked_v2_equals_v1(self):
        v2.check_v2((ROOT / 'build/world/seed4242-v2.img').read_bytes(),
                    (ROOT / 'build/world/seed4242-far.img').read_bytes())


if __name__ == '__main__':
    unittest.main()
