from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from bake_far_field import keep_crown_leaf,mark_rim,root_heights,surface
from bake_large_world import checksum
from convert_large_v2 import V2_MAGIC,encode,decode,check_v2
from leaf_flags_media import upgrade,NORMALIZE,BANK,DIRECTORY,DIRECTORY_END,save_sum
from crown_leaves_media import migrate_disk
from fat_image import FatImage,populate,scratch_hdd


def medium(blocks,shift=0):
    stored=encode(blocks);light=encode(bytes(6400))
    base=193+shift;light_at=base+(len(stored)+511)//512;far=light_at+1;pool=far+1024
    data=bytearray((pool+96)*512);data[:16]=V2_MAGIC
    struct.pack_into('<I',data,16,2);struct.pack_into('<I',data,36,4242)
    struct.pack_into('<I',data,92,0x3246414c)
    index=struct.pack('<3I',base*512,len(stored),checksum(stored))*4096
    data[512:97*512]=index
    data[97*512:193*512]=struct.pack('<3I',light_at*512,len(light),checksum(light))*4096
    struct.pack_into('<I',data,48,checksum(index))
    struct.pack_into('<6I',data,120,base,1,97,pool,2,pool+96)
    data[base*512:base*512+len(stored)]=stored
    data[light_at*512:light_at*512+len(light)]=light
    check_v2(data)
    return data


def saved(data,edited,raw=False):
    result=bytearray(BANK*2);pool=struct.unpack_from('<I',data,132)[0]
    payload=bytes(edited) if raw else encode(edited)
    data[pool*512:pool*512+len(payload)]=payload
    off,n,total=struct.unpack_from('<3I',data,512)
    index_sum=checksum(struct.pack('<I',total)*4096)
    for bank in range(2):
        start=bank*BANK;length=DIRECTORY_END-20+11
        struct.pack_into('<5I',result,start,0x46435354,5,0,length,123-bank)
        result[start+20:start+DIRECTORY]=bytes(range(DIRECTORY-20))
        struct.pack_into('<3I',result,start+DIRECTORY,4242,index_sum,4096)
        for c in range(4096):
            record=(0,n,total,int(n==12288)) if c else (1,len(payload),checksum(payload),int(raw or len(payload)==12288))
            struct.pack_into('<4I',result,start+DIRECTORY+12+c*16,*record)
        result[start+DIRECTORY_END:start+DIRECTORY_END+11]=b'player-tail'
        struct.pack_into('<I',result,start+8,save_sum(result[start+20:start+20+length]))
    return result


class CrownTests(unittest.TestCase):
    def test_only_six_face_neighbors_of_highest_log_qualify(self):
        roots=bytearray(324);roots[8*18+8]=25
        actual={(x,y,z) for z in range(5,10) for x in range(5,10) for y in range(21,28)
                if keep_crown_leaf(roots,x,y,z)}
        self.assertEqual(actual,{(7,25,7),(7,23,7),(6,24,7),(8,24,7),(7,24,6),(7,24,8)})
        self.assertFalse(keep_crown_leaf(roots,6,25,6))
        roots=bytearray(324);roots[8*18]=25
        self.assertTrue(keep_crown_leaf(roots,0,24,7))
        self.assertFalse(keep_crown_leaf(roots,0,25,7))

    def test_flags_preserve_all_voxels_and_keep_five_leaf_crown(self):
        blocks=bytearray(12288)
        for z in range(5,10):
            for x in range(5,10):blocks[(z*16+x)*48+21:(z*16+x)*48+28]=bytes([7])*7
        for y in range(20,25):blocks[(7*16+7)*48+y]=6|32
        roots=bytearray(324)
        for c,h in enumerate(root_heights(blocks)):roots[(c//16+1)*18+c%16+1]=h
        flagged=mark_rim(blocks,roots,crown=True)
        self.assertEqual(flagged.translate(NORMALIZE),blocks)
        self.assertEqual(flagged.count(7),5)
        self.assertEqual(mark_rim(flagged,roots,crown=True),flagged)
        self.assertIn(7,surface(flagged))

    def test_v2_migration_preserves_both_banks_edits_and_relocated_pool(self):
        base=bytearray(12288);base[24]=7;base[48+24]=7;base[23]=6
        crown=bytearray(base);crown[48+24]=71
        for raw in (False,True):
            old=medium(base);new=medium(crown,shift=1)
            edits=bytearray(base);edits[100]=1;edits[24]=0
            save=saved(old,edits,raw=raw);before=bytes(old)
            migrated,updated=upgrade(old,new,save)
            self.assertEqual(old,before)
            self.assertEqual(len(migrated),len(new))
            pool=struct.unpack_from('<I',migrated,132)[0]
            field,n,total,is_raw=struct.unpack_from('<4I',updated,DIRECTORY+12)
            payload=migrated[pool*512:pool*512+n];changed=decode(payload,12288)
            self.assertEqual(changed.translate(NORMALIZE),edits)
            self.assertEqual(changed[48+24],71);self.assertEqual(field,1)
            self.assertEqual(checksum(payload),total)
            self.assertEqual(updated[20:DIRECTORY],save[20:DIRECTORY])
            self.assertEqual(updated[BANK+20:BANK+DIRECTORY],save[BANK+20:BANK+DIRECTORY])
            self.assertEqual(struct.unpack_from('<I',updated,16)[0],123)
            self.assertEqual(struct.unpack_from('<I',updated,BANK+16)[0],122)

    def test_v2_migration_rejects_changed_terrain_and_corrupt_saves(self):
        base=bytearray(12288);base[24]=7
        old=medium(base);save=saved(old,base)
        other=bytearray(base);other[1]=1
        with self.assertRaisesRegex(ValueError,'change terrain'):upgrade(old,medium(other),save)
        broken=bytearray(save);broken[20]^=1
        with self.assertRaisesRegex(ValueError,'save payload'):upgrade(old,medium(base),broken)

    def test_disk_geometry_other_files_and_original_disk_are_preserved(self):
        base=bytearray(12288);base[24]=7
        old=medium(base);save=saved(old,base)
        crown=bytearray(base);crown[24]=71
        disk=scratch_hdd(cylinders=977,heads=5,sectors=17)
        part=struct.unpack_from('<I',disk,454)[0]*512
        disk[part:]=populate(disk[part:part+512],
            [('TERRAIN.TMP',old),('WORLD.SAV',save),('NOTE.TXT',b'preserve me')])
        before=bytes(disk)
        after,terrain,updated=migrate_disk(disk,medium(crown,shift=1))
        fs=FatImage(after,part)
        self.assertEqual(bytes(disk),before)
        self.assertEqual(len(after),len(before));self.assertEqual(after[:part],before[:part])
        self.assertEqual(fs.read('NOTE.TXT'),b'preserve me')
        self.assertEqual(fs.read('TERRAIN.TMP'),terrain)
        self.assertEqual(fs.read('WORLD.SAV'),updated)


if __name__=='__main__':unittest.main()
