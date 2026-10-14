from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from bake_far_field import surface,root_heights,keep_leaf,COMPACT_MAGIC
from large_world_media import FAR_BYTES,LIT_BYTES,identity,check_terrain

class CompactLeafTests(unittest.TestCase):
    def test_nine_stack_footprint_preserves_real_leaf_heights(self):
        blocks=bytearray(12288);roots=bytearray(18*18)
        for z in range(16):
            for x in range(16):blocks[(z*16+x)*48+19]=2
        for y in range(20,25):blocks[(7*16+7)*48+y]=6
        for z in range(5,10):
            for x in range(5,10):blocks[(z*16+x)*48+25]=7
        for z,row in enumerate(root_heights(blocks)):
            roots[(z//16+1)*18+z%16+1]=row
        kept={(x,z) for z in range(16) for x in range(16) if keep_leaf(roots,x,25,z)}
        self.assertEqual(kept,{(x,z) for z in range(6,9) for x in range(6,9)})
        original=surface(blocks);compact=surface(blocks,roots=roots)
        self.assertEqual(len(compact),128)
        self.assertEqual(compact[(4//2*8+4//2)*2:(4//2*8+4//2)*2+2],bytes([20,2]))
        self.assertEqual(compact[(6//2*8+6//2)*2:(6//2*8+6//2)*2+2],bytes([26,7]))
        self.assertGreater(original.count(7),compact.count(7))
        roots[8*18+8]=0
        self.assertFalse(any(keep_leaf(roots,x,25,z) for x,z in kept))

    def test_halo_root_keeps_cross_column_neighbor(self):
        roots=bytearray(324);roots[8*18]=25
        self.assertTrue(keep_leaf(roots,0,25,7))
        self.assertFalse(keep_leaf(roots,1,25,7))
        self.assertFalse(keep_leaf(roots,0,28,7))

    def test_rebaked_fixture_keeps_extents_and_seed(self):
        source=ROOT/'build/checks/large-world-proto/terrain1024-far.bin'
        baked=source.with_name('terrain1024-compact-leaves.bin')
        if not baked.exists():self.skipTest('rebaked fixture required')
        a=source.read_bytes();b=baked.read_bytes();check_terrain(b)
        self.assertEqual(len(b),FAR_BYTES);self.assertEqual(identity(a),identity(b))
        self.assertEqual(a[116:LIT_BYTES],b[116:LIT_BYTES])
        self.assertEqual(struct.unpack_from('<I',b,92)[0],COMPACT_MAGIC)
        self.assertEqual(b[96:116],bytes(20))
        self.assertLess(b[LIT_BYTES+1::2].count(7),a[LIT_BYTES+1::2].count(7))
        self.assertGreater(b[LIT_BYTES+1::2].count(7),0)
        self.assertLessEqual(max(b[LIT_BYTES::2]),48)

if __name__=='__main__':unittest.main()
