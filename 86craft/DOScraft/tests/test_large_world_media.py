"""Lighting upgrades preserve all edit slots and committed saves."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fat_image import FatImage,populate,scratch_hdd
from large_world_media import BASE_BYTES,LIT_BYTES,LIGHT_MAGIC,identity,upgrade_disk

class LightingMigrationTests(unittest.TestCase):
    def test_appendix_upgrade_retains_edits_save_and_seed_identity(self):
        old_terrain=bytearray(BASE_BYTES); old_terrain[:16]=b'DOSCRAFT-1024-V1'
        old_terrain[512:1024]=bytes(range(256))*2
        # Exercise both first and last edit slots, including unused padding.
        old_terrain[57*512:57*512+8]=b'edit one'
        old_terrain[BASE_BYTES-8:]=b'lastedit'
        new_terrain=bytearray(LIT_BYTES); new_terrain[:33*512]=old_terrain[:33*512]
        struct.pack_into('<5I',new_terrain,52,LIGHT_MAGIC,294977,13,1234,6400)
        new_terrain[BASE_BYTES:BASE_BYTES+8]=b'newlight'
        disk=scratch_hdd(cylinders=800,heads=8,sectors=63)
        start=struct.unpack_from('<I',disk,454)[0]*512
        files=[('TERRAIN.TMP',old_terrain),('WORLD.SAV',b'committed V5 bank'),('NOTE.TXT',b'keep me')]
        disk[start:]=populate(disk[start:start+512],files)
        original=bytes(disk)
        upgraded=upgrade_disk(disk,new_terrain)
        volume=FatImage(upgraded,start); terrain=volume.read('TERRAIN.TMP')
        self.assertEqual(identity(terrain),identity(old_terrain))
        self.assertEqual(terrain[:33*512],new_terrain[:33*512])
        self.assertEqual(terrain[33*512:BASE_BYTES],old_terrain[33*512:BASE_BYTES])
        self.assertEqual(terrain[BASE_BYTES:],new_terrain[BASE_BYTES:])
        self.assertEqual(volume.read('WORLD.SAV'),b'committed V5 bank')
        self.assertEqual(volume.read('NOTE.TXT'),b'keep me')
        self.assertEqual(disk,original)
        new_terrain[512]^=1
        with self.assertRaisesRegex(ValueError,'different baked seed'): upgrade_disk(disk,new_terrain)

if __name__=='__main__': unittest.main()
