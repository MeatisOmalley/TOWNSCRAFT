"""Pure blank save-medium fixture tests; no DOS or emulator required."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import save_fixture


class SaveFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = save_fixture.save_image()

    def test_exact_bounded_capacity(self):
        self.assertIsInstance(self.image, bytes)
        self.assertEqual(save_fixture.TRACK_BYTES, 8192)
        self.assertEqual(save_fixture.NUM_TRACKS, 154)
        self.assertEqual(save_fixture.BANK_TRACKS, 77)
        self.assertEqual(save_fixture.SAVE_BYTES, 1261568)
        self.assertEqual(len(self.image), 77 * 2 * 8 * 1024)
        self.assertEqual(len(self.image), save_fixture.SAVE_BYTES)
        self.assertEqual(len(self.image),
                         save_fixture.NUM_TRACKS * save_fixture.TRACK_BYTES)

    def test_two_equal_disjoint_banks_cover_the_medium(self):
        bank_bytes = save_fixture.BANK_TRACKS * save_fixture.TRACK_BYTES
        self.assertEqual(bank_bytes, 630784)
        self.assertEqual(save_fixture.NUM_TRACKS, 2 * save_fixture.BANK_TRACKS)
        previous_end = 0
        banks = []
        for bank in range(2):
            start = bank * bank_bytes
            end = start + bank_bytes
            with self.subTest(bank=bank):
                self.assertEqual(start, previous_end)
                self.assertEqual(start % save_fixture.TRACK_BYTES, 0)
                self.assertLessEqual(end, len(self.image))
                banks.append(self.image[start:end])
                self.assertEqual(len(banks[-1]), bank_bytes)
                previous_end = end
        self.assertEqual(previous_end, len(self.image))
        self.assertEqual(banks[0], banks[1])

    def test_entire_medium_is_zero_without_headers_or_envelope(self):
        self.assertEqual(self.image, bytes(1261568))

    def test_repeat_calls_return_deterministic_bytes(self):
        self.assertEqual(save_fixture.save_image(), self.image)
        self.assertEqual(save_fixture.save_image(), self.image)

    def test_generation_does_not_open_files(self):
        with patch('builtins.open', side_effect=AssertionError('unexpected I/O')):
            self.assertEqual(save_fixture.save_image(), self.image)
