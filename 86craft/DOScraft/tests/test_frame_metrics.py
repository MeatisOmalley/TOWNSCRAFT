from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from frame_metrics import frame_metrics


class FrameMetricsTests(unittest.TestCase):
    def test_average_uses_elapsed_time_not_mean_instantaneous_fps(self):
        result=frame_metrics([10000,30000])
        self.assertEqual(result['average_fps'],50)
        self.assertAlmostEqual(result['1_percent_low_fps'],1000/30)
        self.assertFalse(result['1_percent_minimum_sample_count_met'])
        self.assertFalse(result['0_1_percent_minimum_sample_count_met'])

    def test_tail_average_not_percentile_reciprocal(self):
        result=frame_metrics([10000]*9900+[20000]*90+[30000]*9+[100000])
        self.assertEqual(result['1_percent_tail_frames'],100)
        self.assertEqual(result['0_1_percent_tail_frames'],10)
        self.assertAlmostEqual(result['1_percent_low_fps'],100*1e6/2170000)
        self.assertAlmostEqual(result['0_1_percent_low_fps'],10*1e6/370000)
        self.assertTrue(result['0_1_percent_tail_has_at_least_10_frames'])

    def test_fraction_rounds_up_and_marks_sample_support(self):
        result=frame_metrics([10000]*1001)
        self.assertEqual(result['1_percent_tail_frames'],11)
        self.assertEqual(result['0_1_percent_tail_frames'],2)
        self.assertTrue(result['0_1_percent_minimum_sample_count_met'])
        self.assertFalse(result['0_1_percent_tail_has_at_least_10_frames'])

    def test_invalid_inputs(self):
        for values in ([],[0],[-1],[1.5]):
            with self.subTest(values=values),self.assertRaises(ValueError): frame_metrics(values)
