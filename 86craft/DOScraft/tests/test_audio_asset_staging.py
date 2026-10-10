"""Bounded audio source selections/provenance; playback is not implemented."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import audio_assets
import build_imported as imported


class AudioSelectionTests(unittest.TestCase):
    def test_exact_replay_preserves_generators_parameters_score_and_sort(self):
        generated,row=audio_assets.asset_source()
        source=(imported.VENDOR/row['source']).read_bytes()
        selected=b''.join(source[slice(*row['selections'][p['source']])] if 'source' in p
                          else p['literal'].encode('ascii') for p in row['assembly'])
        for step in row['transformations']:
            before,after=step['before'].encode(),step['after'].encode()
            self.assertEqual(selected.count(before),step['count'])
            selected=selected.replace(before,after)
        self.assertEqual(generated,selected)
        self.assertEqual(row['staged_sha256'],imported.sha256(generated))
        self.assertEqual(row['source_sha256'],imported.sha256(source))
        self.assertEqual(len(row['transformations']),1)
        self.assertEqual(row['transformations'][0]['before'],'static int synthTop,synthBank=-1;')
        for token in (b'gen_noise(SFX_CRUNCH,2600,2,60,180,256,200);',
                      b'gen_noise(SFX_EXPLODE,14000,4,40,500,128,255);',
                      b'#define EIGHTH 36',b'evTick[j-1]>t'):
            self.assertIn(token,generated)
        for token in (b'outb(',b'inb(',b'PCM_WINDOW',b'fm_write(',b'cli()',b'sti()'):
            self.assertNotIn(token,generated)

    def test_modified_sound_or_pin_is_refused_before_output_creation(self):
        with tempfile.TemporaryDirectory() as name:
            folder=Path(name); vendor=folder/'vendor'; (vendor/'src').mkdir(parents=True)
            manifest=(imported.VENDOR/'MANIFEST.json').read_bytes()
            original=(imported.VENDOR/'src/sound.c').read_bytes()
            (vendor/'MANIFEST.json').write_bytes(manifest)
            (vendor/'src/sound.c').write_bytes(original+b'\n/* modified */')
            with self.assertRaisesRegex(ValueError,'provenance mismatch'):
                audio_assets.stage(folder/'output',vendor)
            self.assertFalse((folder/'output').exists())
            wrong=json.loads(manifest); wrong['source_commit']='0'*40
            (vendor/'MANIFEST.json').write_text(json.dumps(wrong))
            (vendor/'src/sound.c').write_bytes(original)
            with self.assertRaisesRegex(ValueError,'provenance mismatch'):
                audio_assets.stage(folder/'output',vendor)
            self.assertFalse((folder/'output').exists())

    def test_stage_retains_vendor_and_records_audio_selection(self):
        before=(imported.VENDOR/'src/sound.c').read_bytes()
        with tempfile.TemporaryDirectory() as name:
            folder=Path(name); report=audio_assets.stage(folder)
            row=report['audio_selection']
            self.assertEqual((folder/row['staged']).read_bytes(),audio_assets.asset_source()[0])
            self.assertEqual(report['source_commit'],imported.PINNED_COMMIT)
        self.assertEqual((imported.VENDOR/'src/sound.c').read_bytes(),before)


if __name__=='__main__': unittest.main()
