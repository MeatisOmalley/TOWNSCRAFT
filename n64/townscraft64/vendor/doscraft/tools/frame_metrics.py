"""FPS from elapsed frame durations, never an average of instantaneous FPS."""
import math


def frame_metrics(frames_us):
    frames = list(frames_us)
    if not frames or any(not isinstance(x, int) or x <= 0 for x in frames):
        raise ValueError('Need positive integer frame durations in microseconds')
    slowest = sorted(frames, reverse=True)
    result = {'sample_count': len(frames),
              'average_fps': len(frames) * 1_000_000 / sum(frames),
              'low_definition': 'Reciprocal of mean frame time of slowest ceil(N * fraction) frames'}
    for label, denominator in (('1_percent', 100), ('0_1_percent', 1000)):
        count = math.ceil(len(frames) / denominator)
        tail = slowest[:count]
        result[label + '_low_fps'] = count * 1_000_000 / sum(tail)
        result[label + '_tail_frames'] = count
        result[label + '_minimum_sample_count_met'] = len(frames) >= denominator
        result[label + '_tail_has_at_least_10_frames'] = count >= 10
    return result
