from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))


class DayLengthTests(unittest.TestCase):
    def test_one_clock_step_per_tick_and_replayable(self):
        import build_imported
        import dos_day_length
        data = (build_imported.VENDOR / 'src/game.c').read_bytes()
        result, steps = dos_day_length.adapt('game.c', data)
        replay = data
        for step in steps:
            self.assertEqual(replay.count(step['before'].encode()), step['count'])
            replay = replay.replace(step['before'].encode(), step['after'].encode())
        self.assertEqual(result, replay)
        self.assertIn(b'g_time=(g_time+1)%24000;', result)
        self.assertNotIn(b'g_time=(g_time+2)%24000;', result)
        # 24000 steps at 20 game ticks a second: 20 minutes, as in Minecraft.
        self.assertEqual(24000 / 1 / 20 / 60, 20)
        self.assertEqual(dos_day_length.adapt('world.c', b'x'), (b'x', []))


if __name__ == '__main__':
    unittest.main()
