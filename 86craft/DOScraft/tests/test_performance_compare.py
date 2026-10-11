import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from performance_compare import compare,compact

class CompareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.evidence=json.loads((ROOT/'tests/benchmarks/2026-10-10/pack-all.json').read_text())

    def test_real_recorded_three_repeat_comparison(self):
        rows=compare(self.evidence['baseline'],self.evidence['candidate'])
        self.assertEqual(len(rows),6)
        self.assertTrue(all(row['frame_time_reduction_percent']>7 for row in rows))
        self.assertEqual(rows[0]['baseline_fps']['sample_count'],96)
        self.assertFalse(rows[0]['baseline_fps']['0_1_percent_minimum_sample_count_met'])
        self.assertEqual(len(rows[0]['baseline_repeat_fps']),3)

    def test_compact_contains_per_scene_and_combined_fps(self):
        result=compact(self.evidence['baseline'])
        self.assertEqual(result['fps_overall']['sample_count'],576)
        self.assertEqual(len(result['fps_by_scene']),6)
        self.assertTrue(all(x['sample_count']==96 for x in result['fps_by_scene'].values()))

    def test_reject_different_sampling_workloads(self):
        for change in ('count','policy','frames'):
            candidate=copy.deepcopy(self.evidence['candidate'])
            if change=='count': candidate['results'][0]['header']['samples_per_scene']='1000'
            if change=='policy': candidate['results'][1]['header']['motion_policy']='unbounded_walk'
            if change=='frames': candidate['results'][2]['scenes'][0]['frames'].append(100000)
            with self.subTest(change=change),self.assertRaises(ValueError):
                compare(self.evidence['baseline'],candidate)

    def test_reject_reused_fixture_against_fresh_world(self):
        candidate=copy.deepcopy(self.evidence['candidate'])
        for run in candidate['results']:
            run['header']['replay_context']='POST_FIXTURE'
        with self.assertRaisesRegex(ValueError,'Different replay context'):
            compare(self.evidence['baseline'],candidate)

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
        candidate['results'][0]['header']['wall_verification']='TIMINGS_INVALID'
        with self.assertRaises(ValueError): compare(self.evidence['baseline'],candidate)
        candidate['results'][0]['header'].pop('wall_verification')
        candidate['results'][0]['header']['f2b_verification']='TIMINGS_INVALID'
        with self.assertRaises(ValueError): compare(self.evidence['baseline'],candidate)
        candidate['results'][0]['header'].pop('f2b_verification')
        candidate['results'][0]['header']['draw_profiling']='TIMINGS_DIAGNOSTIC_ONLY'
        with self.assertRaises(ValueError): compare(self.evidence['baseline'],candidate)
        candidate['results'][0]['header'].pop('draw_profiling')
        candidate['results'][0]['header']['frame_dumps']='TIMINGS_DIAGNOSTIC_ONLY'
        with self.assertRaises(ValueError): compare(self.evidence['baseline'],candidate)

    def test_explicit_visual_review_keeps_hash_and_repeat_guards(self):
        baseline=self.evidence['baseline']; candidate=copy.deepcopy(self.evidence['candidate'])
        name=candidate['results'][0]['scenes'][0]['name']
        for run in candidate['results']: run['scenes'][0]['hash']='reviewed'
        review={name:{'baseline_hash':baseline['results'][0]['scenes'][0]['hash'],
            'candidate_hash':'reviewed','note':'Small texture-edge change permitted at Low',
            'evidence':'retained/pixel-difference-report.json'}}
        with self.assertRaises(ValueError): compare(baseline,candidate)
        rows=compare(baseline,candidate,image_review=review)
        self.assertFalse(rows[0]['image_match'])
        self.assertEqual(rows[0]['candidate_hash'],'reviewed')
        self.assertEqual(rows[0]['image_review'],review[name])
        self.assertTrue(all(r['image_match'] for r in rows[1:]))
        for change in ('hash','unstable','unreviewed','hardware','fail','note','unknown'):
            modified=copy.deepcopy(candidate); approval=copy.deepcopy(review)
            if change=='hash': approval[name]['candidate_hash']='other'
            if change=='unstable': modified['results'][1]['scenes'][0]['hash']='other'
            if change=='unreviewed':
                for run in modified['results']: run['scenes'][1]['hash']='other'
            if change=='hardware': modified['manifest']['profile_sha256']='other'
            if change=='fail': modified['results'][0]['header']['result']='FAIL'
            if change=='note': approval[name]['note']=' '
            if change=='unknown': approval['unknown']=approval[name]
            with self.subTest(change=change),self.assertRaises(ValueError):
                compare(baseline,modified,image_review=approval)

if __name__=='__main__': unittest.main()
