from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import publish_game as publisher

class PublishTests(unittest.TestCase):
    def test_preserves_player_disk_old_vm_and_iso_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); old=root/'runtime/old'; new=root/'runtime/new'
            out=root/'build'; iso=root/'ISO/DOSCRAFT.ISO'
            old.mkdir(parents=True); new.mkdir(); out.mkdir(); iso.parent.mkdir()
            iso.write_bytes(b'old iso'); (old/'scratch.img').write_bytes(b'player edits and mobs')
            previous={'exe_sha256':'a'*64,'iso_sha256':publisher.sha(iso),'smoke':False}
            (old/'build.json').write_text(json.dumps(previous))
            (out/'build.json').write_text(json.dumps({'benchmark':False,'exe_sha256':'b'*64,
                'smoke_verified_exe_sha256':'b'*64}))
            pointer=out/'latest-play-vm.txt'; pointer.write_text(str(old))
            (new/'DOSCRAFT.ISO').write_bytes(b'optimized iso'); (new/'scratch.img').write_bytes(b'blank')
            record={'iso_sha256':publisher.sha(new/'DOSCRAFT.ISO')}
            with patch.object(publisher,'ROOT',root),patch.object(publisher,'OUTPUT',out),\
                 patch.object(publisher,'running',return_value=False),\
                 patch.object(publisher,'prepare',return_value=(new,record)),\
                 patch.object(publisher,'validate',return_value=[]):
                result=publisher.publish('fake bios')
            self.assertEqual(result,new)
            self.assertEqual((new/'scratch.img').read_bytes(),(old/'scratch.img').read_bytes())
            self.assertEqual((old/'build.json').read_text(),json.dumps(previous))
            self.assertEqual(iso.read_bytes(),b'optimized iso')
            self.assertEqual(pointer.read_text(),str(new))
            self.assertEqual(iso.with_name('DOSCRAFT.before-'+('a'*12)+'.ISO').read_bytes(),b'old iso')

    def test_atomic_write_replaces_only_named_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'pointer'; path.write_bytes(b'old')
            publisher.atomic_write(path,b'new')
            self.assertEqual(path.read_bytes(),b'new')
            self.assertEqual(list(Path(directory).iterdir()),[path])

    def test_live_player_vm_refuses_before_preparing_or_publishing(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); old=root/'runtime/old'; out=root/'build'; iso=root/'ISO/DOSCRAFT.ISO'
            old.mkdir(parents=True); out.mkdir(); iso.parent.mkdir(); iso.write_bytes(b'old')
            (old/'build.json').write_text(json.dumps({'iso_sha256':publisher.sha(iso)}))
            (out/'latest-play-vm.txt').write_text(str(old))
            (out/'build.json').write_text(json.dumps({'exe_sha256':'b'*64,'smoke_verified_exe_sha256':'b'*64}))
            with patch.object(publisher,'ROOT',root),patch.object(publisher,'OUTPUT',out),\
                 patch.object(publisher,'running',return_value=True),patch.object(publisher,'prepare') as prepare:
                with self.assertRaisesRegex(ValueError,'disk is live'): publisher.publish('bios')
                prepare.assert_not_called()
            self.assertEqual(iso.read_bytes(),b'old')

if __name__=='__main__': unittest.main()
