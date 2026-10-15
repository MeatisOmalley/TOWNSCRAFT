from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from bake_large_world import checksum
from compact_world_media import convert_player
from convert_large_v2 import decode as unpack,check_v2
from compact_world_media import grow_edit_pool
from leaf_flags_media import BANK,DIRECTORY,DIRECTORY_END,save_sum,encode
from large_world_media import FAR_BYTES,LIT_BYTES,LIGHT_MAGIC,FAR_MAGIC,FAR_MAP_BYTES


class CompactPlayerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        data=bytearray(FAR_BYTES);data[:16]=b'DOSCRAFT-1024-V1'
        struct.pack_into('<I',data,16,1);struct.pack_into('<I',data,36,4242)
        struct.pack_into('<5I',data,52,LIGHT_MAGIC,294977,13,0,6400)
        struct.pack_into('<5I',data,72,FAR_MAGIC,LIT_BYTES//512,FAR_MAP_BYTES,0,1)
        total=checksum(bytes(12288));data[512:33*512]=struct.pack('<I',total)*4096
        index=checksum(data[512:33*512]);struct.pack_into('<I',data,48,index)
        save=bytearray(BANK*2);edited={}
        for c,slot,blocks in [(0,1,bytes([7])*12288),(0,2,bytes([71])*12288),
                               (1,1,bytes([1,2])*6144)]:
            payload,raw=encode(blocks);at=(33+c*72+slot*24)*512
            data[at:at+len(payload)]=payload;edited[c,slot]=(payload,raw,blocks)
        for bank in range(2):
            start=bank*BANK;length=DIRECTORY_END-20+9
            struct.pack_into('<5I',save,start,0x46435354,5,0,length,10+bank)
            save[start+20:start+DIRECTORY]=bytes(range(DIRECTORY-20))
            struct.pack_into('<3I',save,start+DIRECTORY,4242,index,4096)
            for c in range(4096):
                slot=(bank+1 if c==0 else 1 if c==1 else 0)
                if slot:
                    payload,raw,blocks=edited[c,slot];record=(slot,len(payload),checksum(payload),raw)
                else:record=(0,12288,total,1)
                struct.pack_into('<4I',save,start+DIRECTORY+12+c*16,*record)
            save[start+DIRECTORY_END:start+DIRECTORY_END+9]=b'keep-tail'
            struct.pack_into('<I',save,start+8,save_sum(save[start+20:start+20+length]))
        cls.before=bytes(data);cls.save=bytes(save);cls.edited=edited
        cls.after,cls.updated,cls.proof=convert_player(cls.before,cls.save)

    def test_base_lighting_and_far_roundtrip(self):
        fresh=bytearray(self.before)
        for c in range(4096):fresh[(33+c*72+24)*512:(33+c*72+72)*512]=bytes(24576)
        check_v2(self.after,fresh)
        self.assertLess(len(self.after),len(self.before))
        self.assertEqual(self.proof['edited_snapshots'],3)
        self.assertEqual(self.proof['save_banks'],2)

    def test_both_banks_raw_rle_edits_and_player_state_survive(self):
        pool=struct.unpack_from('<I',self.after,132)[0]
        for bank in range(2):
            start=bank*BANK
            self.assertEqual(self.updated[start+20:start+DIRECTORY],self.save[start+20:start+DIRECTORY])
            self.assertEqual(struct.unpack_from('<I',self.updated,start+16)[0],10+bank)
            for c in [0,1]:
                field,n,total,raw=struct.unpack_from('<4I',self.updated,start+DIRECTORY+12+c*16)
                slot,pair=field&3,field>>2;at=(pool+(pair*2+slot-1)*24)*512
                payload=self.after[at:at+n]
                self.assertEqual(checksum(payload),total)
                self.assertEqual(unpack(payload,12288),self.edited[c,slot][2])
        self.assertEqual(self.before[:16],b'DOSCRAFT-1024-V1')

    def test_corrupt_committed_save_and_edited_payload_are_rejected(self):
        bad=bytearray(self.save);bad[20]^=1
        with self.assertRaisesRegex(ValueError,'committed'):convert_player(self.before,bad)
        bad=bytearray(self.before);bad[57*512]^=1
        with self.assertRaisesRegex(ValueError,'edit slot'):convert_player(bad,self.save)

    def test_full_capacity_pool_growth_preserves_every_existing_byte_and_slot(self):
        full=grow_edit_pool(self.after,4096)
        self.assertEqual(struct.unpack_from('<I',full,136)[0],4096)
        self.assertEqual(full[:136],self.after[:136])
        self.assertEqual(full[144:len(self.after)],self.after[144:])
        self.assertFalse(any(full[len(self.after):]))
        self.assertEqual(len(full)-len(self.after),(4096-self.proof['pool_pairs'])*24576)
        with self.assertRaisesRegex(ValueError,'shrink'):grow_edit_pool(full,384)


if __name__=='__main__':unittest.main()
