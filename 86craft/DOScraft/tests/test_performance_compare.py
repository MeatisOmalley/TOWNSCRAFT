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

    def test_occlusion_investigation_is_explicit_and_still_checks_pixels(self):
        baseline=self.evidence['baseline']; candidate=copy.deepcopy(self.evidence['candidate'])
        for run in candidate['results']: run['header']['occlusion']='1'
        with self.assertRaises(ValueError): compare(baseline,candidate)
        self.assertEqual(len(compare(baseline,candidate,('occlusion',))),6)
        candidate['results'][1]['header']['occlusion']='0'
        with self.assertRaises(ValueError): compare(baseline,candidate,('occlusion',))
        candidate['results'][1]['header']['occlusion']='1'
        candidate['results'][2]['scenes'][0]['hash']='wrong'
        with self.assertRaises(ValueError): compare(baseline,candidate,('occlusion',))
        with self.assertRaises(ValueError): compare(baseline,candidate,('scale',))

    def test_reject_reference_redraw_timings(self):
        candidate=copy.deepcopy(self.evidence['candidate'])
        candidate['results'][0]['header']['f2b_verification']='TIMINGS_INVALID'
        with self.assertRaises(ValueError): compare(self.evidence['baseline'],candidate)
        candidate['results'][0]['header'].pop('f2b_verification')
        candidate['results'][0]['header']['draw_profiling']='TIMINGS_DIAGNOSTIC_ONLY'
        with self.assertRaises(ValueError): compare(self.evidence['baseline'],candidate)

if __name__=='__main__': unittest.main()
