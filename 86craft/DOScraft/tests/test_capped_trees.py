from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from bake_far_field import surface,append_map,CAP_MAGIC
from large_world_media import FAR_BYTES,LIT_BYTES,identity,check_terrain
TERRAIN=ROOT/'build/checks/large-world-proto/terrain1024-far.bin'

class CappedTreeTests(unittest.TestCase):
    def test_height_bits_encode_natural_crown_without_another_record(self):
        blocks=bytearray(12288)
        for y in range(20):blocks[y]=2
        blocks[20:25]=bytes([6])*5;blocks[25:27]=bytes([7])*2
        plain=surface(blocks);capped=surface(blocks,caps=True)
        self.assertEqual(len(capped),len(plain));self.assertEqual(capped[:2],bytes([25|(2<<6),6]))
        self.assertEqual(capped[2:],plain[2:])
        blocks[24]=6|64
        self.assertEqual(surface(blocks,caps=True)[:2],bytes([25,6]))
        blocks[24]=6;blocks[25]=0
        self.assertEqual(surface(blocks,caps=True)[:2],bytes([25,6]))

    @unittest.skipUnless(TERRAIN.is_file(),'existing prebaked seed required')
    def test_capped_map_stays_the_same_size_and_preserves_seed(self):
        original=TERRAIN.read_bytes();data=append_map(original,caps=True);check_terrain(data)
        self.assertEqual(len(data),FAR_BYTES);self.assertEqual(identity(data),identity(original))
        self.assertEqual(struct.unpack_from('<I',data,92)[0],CAP_MAGIC)
        self.assertFalse(any(data[96:116]))
        summary=data[LIT_BYTES:];count=0
        for i in range(0,len(summary),2):
            h,b=summary[i:i+2];self.assertLessEqual(h&63,48);self.assertNotEqual(b,7)
            if h>>6:
                count+=1;self.assertEqual(b,6);self.assertLessEqual((h&63)+(h>>6),48)
        self.assertGreater(count,0)

if __name__=='__main__':unittest.main()
