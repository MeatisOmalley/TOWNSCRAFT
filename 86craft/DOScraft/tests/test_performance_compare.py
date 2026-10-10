import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from performance_compare import compare

class CompareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.evidence=json.loads((ROOT/'tests/benchmarks/2026-10-10/pack-all.json').read_text())

    def test_real_recorded_three_repeat_comparison(self):
        rows=compare(self.evidence['baseline'],self.evidence['candidate'])
        self.assertEqual(len(rows),6)
        self.assertTrue(all(row['frame_time_reduction_percent']>7 for row in rows))

    def test_reject_different_images_hardware_settings_or_failed_runs(self):
        for change in ('hash','hardware','scale','fail','repeats'):
            candidate=copy.deepcopy(self.evidence['candidate'])
            if change=='hash': candidate['results'][1]['scenes'][4]['hash']='00000000'
            if change=='hardware': candidate['manifest']['profile_sha256']='different'
            if change=='scale': candidate['results'][2]['header']['scale']='1'
            if change=='fail': candidate['results'][0]['header']['result']='FAIL'
            if change=='repeats': candidate['results'].pop()
            with self.subTest(change=change),self.assertRaises(ValueError):
                compare(self.evidence['baseline'],candidate)

if __name__=='__main__': unittest.main()
