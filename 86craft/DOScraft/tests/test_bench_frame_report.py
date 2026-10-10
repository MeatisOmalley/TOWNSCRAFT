from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from bench_frame_report import pixel_hash,difference


class FrameDumpTests(unittest.TestCase):
    def test_fnv_reference(self):
        self.assertEqual(pixel_hash(b''),'811c9dc5')
        self.assertEqual(pixel_hash(b'hello'),'4f9f2cab')

    def test_exact_pixel_palette_and_region_bounds(self):
        old=bytes(768+320*240); new=bytearray(old)
        new[4]=16; new[768+7*320+5]=3; new[768+239*320+319]=9
        result=difference(old,new)
        self.assertEqual(result['changed_pixels'],2)
        self.assertEqual(result['changed_view_pixels'],1)
        self.assertEqual(result['changed_hud_pixels'],1)
        self.assertEqual(result['palette_bytes_changed'],1)
        self.assertEqual(result['bounds'],[5,7,319,239])
        self.assertEqual(result['first_changed_pixels'][0],{'x':5,'y':7,'baseline':0,'candidate':3})
        self.assertIsNone(difference(old,old)['bounds'])
        with self.assertRaises(ValueError): difference(old,new[:-1])
