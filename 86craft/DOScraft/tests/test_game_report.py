"""Independent parsing/PNG diagnostic helpers, not gameplay acceptance."""
from pathlib import Path
import struct
import sys
import unittest
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from game_report import parse_v3, frame_png
from world_storage_reference import worldio_expected_image


class ReportTests(unittest.TestCase):
    def test_known_independent_real_world_storage_fixture(self):
        result=parse_v3(worldio_expected_image())
        self.assertEqual(result['width'],256)
        self.assertEqual(result['player_xyz'],[48*4096,2*4096,48*4096])
        self.assertEqual((result['active_mobs'],result['dormant_mobs']),(1,1))
        self.assertEqual(result['chests'],1)

    def test_corruption_or_incomplete_header_rejected(self):
        original=worldio_expected_image()
        for offset in (0,4,8,20,148):
            bad=bytearray(original); bad[offset]^=1
            with self.subTest(offset=offset),self.assertRaises(ValueError): parse_v3(bad)
        with self.assertRaises(ValueError): parse_v3(original[:-1])

    def test_frame_png_pixel_palette_and_crc_readback(self):
        raw=bytes(range(256))*3+bytes(range(256))*300
        encoded=frame_png(raw); at=8; chunks={}
        self.assertEqual(encoded[:8],b'\x89PNG\r\n\x1a\n')
        while at<len(encoded):
            length=struct.unpack_from('>I',encoded,at)[0]
            kind=encoded[at+4:at+8]; data=encoded[at+8:at+8+length]
            self.assertEqual(struct.unpack_from('>I',encoded,at+8+length)[0],zlib.crc32(kind+data)&0xffffffff)
            chunks[kind]=data; at+=length+12
        self.assertEqual(chunks[b'PLTE'],raw[:768])
        scan=zlib.decompress(chunks[b'IDAT'])
        self.assertEqual(len(scan),321*240)
        self.assertEqual(b''.join(scan[y*321+1:(y+1)*321] for y in range(240)),raw[768:])

    def test_bad_snapshot_size_rejected(self):
        for size in (0,768,77567,77569):
            with self.subTest(size=size),self.assertRaises(ValueError): frame_png(bytes(size))


if __name__=='__main__': unittest.main()
