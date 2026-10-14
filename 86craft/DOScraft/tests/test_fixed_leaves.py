from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from bake_far_field import mark_rim,surface,LEAF_RIM,LEAF_FLAGS_MAGIC
from leaf_flags_media import NORMALIZE,encode,decode,validate_save_preservation,save_sum,BANK,DIRECTORY,DIRECTORY_END
from large_world_media import LIT_BYTES,FAR_BYTES

class FixedLeavesTests(unittest.TestCase):
    def test_baked_flags_keep_nine_columns_and_all_real_voxels(self):
        blocks=bytearray(12288);roots=bytearray(324);roots[8*18+8]=25
        for z in range(5,10):
            for x in range(5,10):blocks[(z*16+x)*48+25:(z*16+x)*48+27]=bytes([7,7])
        flagged=mark_rim(blocks,roots)
        self.assertEqual(len(flagged),len(blocks));self.assertEqual(flagged.translate(NORMALIZE),blocks)
        self.assertEqual(flagged.count(7),9*2);self.assertEqual(flagged.count(7|LEAF_RIM),16*2)
        self.assertEqual(mark_rim(flagged,roots),flagged)
        self.assertEqual(surface(flagged),surface(blocks,roots=roots))
        encoded,raw=encode(flagged);self.assertEqual(decode(encoded,raw),flagged)

    def test_fixed_fixture_keeps_lighting_and_surface_result(self):
        old=ROOT/'build/checks/large-world-proto/terrain1024-far.bin'
        fixed=old.with_name('terrain1024-fixed-leaves.bin');dynamic=old.with_name('terrain1024-compact-leaves.bin')
        if not fixed.exists():self.skipTest('fixed fixture required')
        a=old.read_bytes();b=fixed.read_bytes()
        self.assertEqual(len(b),FAR_BYTES);self.assertEqual(a[16:48],b[16:48])
        self.assertEqual(a[151011840:LIT_BYTES],b[151011840:LIT_BYTES])
        self.assertEqual(struct.unpack_from('<I',b,92)[0],LEAF_FLAGS_MAGIC)
        if dynamic.exists():self.assertTrue(b[LIT_BYTES:]==dynamic.read_bytes()[LIT_BYTES:],'initial clipped map changed')

    def test_save_validation_rejects_player_or_slot_changes(self):
        before=bytearray(BANK*2);length=DIRECTORY_END-20
        struct.pack_into('<5I',before,0,0x46435354,5,0,length,7)
        struct.pack_into('<I',before,8,save_sum(before[20:20+length]))
        changed=bytearray(before);changed[20]=1
        with self.assertRaises(ValueError):validate_save_preservation(before,changed)
        changed=bytearray(before);changed[DIRECTORY+12]=1
        with self.assertRaises(ValueError):validate_save_preservation(before,changed)

if __name__=='__main__':unittest.main()
