from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from bake_tree_canopies import append_trees
from bake_large_world import checksum
from bake_far_field import surface
from large_world_media import FAR_BYTES,FAR_MAP_BYTES,TREE_MAGIC,check_terrain,identity
TERRAIN=ROOT/'build/checks/large-world-proto/terrain1024-far.bin'

class TreeCanopyTests(unittest.TestCase):
    @unittest.skipUnless(TERRAIN.is_file(),'existing prebaked seed required')
    def test_sparse_ground_provenance_and_unchanged_panorama(self):
        original=TERRAIN.read_bytes();data,report=append_trees(original);check_terrain(data)
        self.assertEqual(data[512:FAR_BYTES],original[512:FAR_BYTES])
        self.assertEqual(identity(original),identity(data))
        magic,sector,size,total,cells,trees=struct.unpack_from('<6I',data,92)
        self.assertEqual((magic,sector),(TREE_MAGIC,FAR_BYTES//512))
        payload=data[FAR_BYTES:];self.assertEqual(checksum(payload),total)
        self.assertLess(size,FAR_MAP_BYTES);self.assertGreater(trees,0)
        base=(65536+cells*2+3)&~3
        columns=struct.unpack_from('<4097I',payload,base)
        self.assertEqual(columns[0],0);self.assertEqual(columns[-1],trees)
        self.assertEqual(sorted(columns),list(columns))
        for column in (0,511,1024,2048,3072,4095):
            offset=(33+column*72)*512
            ground=surface(original[offset:offset+12288],leaves=False)
            cx,cz=column%64,column//64
            for z in range(8):
                for x in range(8):
                    cell=(cz*8+z)*512+cx*8+x
                    mask,first=struct.unpack_from('<II',payload,(cell>>5)*8)
                    bit=1<<(cell&31)
                    self.assertEqual(bool(mask&bit),original[FAR_BYTES-FAR_MAP_BYTES+cell*2+1]==7)
                    if mask&bit:
                        index=first+(mask&(bit-1)).bit_count()
                        self.assertEqual(payload[65536+index*2:65538+index*2],ground[(z*8+x)*2:(z*8+x)*2+2])
        bad=bytearray(data);struct.pack_into('<I',bad,100,size+512)
        with self.assertRaisesRegex(ValueError,'natural-tree'):check_terrain(bad)
        self.assertTrue(report['panorama_unchanged'])

if __name__=='__main__':unittest.main()
