from pathlib import Path
from hashlib import sha256
import json
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from preview_block_textures import generate
from dos_block_textures import adapt
from build_imported import VENDOR
from import_eagler_textures import ROOT,convert,palette_bytes
from compare_eagler_textures import names

class BlockTextureTests(unittest.TestCase):
    def test_imported_pixels_resolution_switch_and_atlas_contract(self):
        with tempfile.TemporaryDirectory(prefix='doscraft-texture-test-') as directory:
            folder=Path(directory);generate(folder)
            before=(folder/'before.bin').read_bytes();after=(folder/'after.bin').read_bytes()
            self.assertEqual(len(before),len(after))
            self.assertEqual(before[:768],after[:768])
            self.assertNotEqual(before[768:768+10*256],after[768:768+10*256])
            simple=(folder/'simple.bin').read_bytes()
            self.assertEqual(simple[:768],after[:768])
            self.assertEqual(len(simple),len(after))
            # The linked generator must actually use the imported pixels.
            packed=(ROOT/'assets/textures/eagler_full.bin').read_bytes()
            metadata=json.loads((ROOT/'assets/textures/eagler_full.json').read_text())
            self.assertEqual(after[:768],palette_bytes())
            ids=names();self.assertEqual(len(metadata['tiles']),len(ids)-1)
            for tile in metadata['tiles']:
                i=tile['index'];actual=after[768+i*256:768+(i+1)*256]
                self.assertEqual(actual,packed[i*256:(i+1)*256],tile['texture'])
                self.assertEqual(sha256(actual).hexdigest(),tile['converted_sha256'])
            # The local EPK is only needed for import/provenance verification.
            source=ROOT/'build/checks/biome-texture-study/assets-unpacked.bin'
            if source.exists():self.assertEqual(convert(source)[0],packed)
            # Small-view alpha silhouettes remain unchanged from our original art.
            for i,name in enumerate(ids):
                if 'CHICKEN' in name or name=='T_I_FEATHER':continue
                start=768+i*256;end=start+256
                self.assertEqual([j for j,b in enumerate(before[start:end]) if b==0],
                                 [j for j,b in enumerate(simple[start:end]) if b==0],name)

    def test_source_overlay_replays_exactly(self):
        before=(VENDOR/'src/textures.c').read_bytes();after,recipe=adapt('textures.c',before)
        replay=before
        for step in recipe:
            a,b=step['before'].encode(),step['after'].encode()
            self.assertEqual(replay.count(a),step['count']);replay=replay.replace(a,b)
        self.assertEqual(replay,after)

if __name__=='__main__':unittest.main()
