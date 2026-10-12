from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from a1_census import analyze


def floor(x, z, y, direction, material):
    word_a = x | (z << 8) | (y << 16) | (direction << 22)
    return (word_a, material, 0, 1)


class A1CensusTests(unittest.TestCase):
    def test_material_boundary_and_painter_interruption_are_distinct(self):
        records = [
            floor(1, 1, 4, 2, 7),
            floor(1, 1, 5, 3, 7),  # Different plane interrupts painter order.
            floor(2, 1, 4, 2, 7),
            floor(3, 1, 4, 2, 8),  # Same plane, different texture/light key.
        ]
        row = analyze(records)
        self.assertEqual(row['visible_terrain_items'], 4)
        self.assertEqual(row['coplanar_direction_groups'], 2)
        self.assertEqual(row['groups_with_multiple_texture_or_light_keys'], 1)
        self.assertEqual(row['optimistic_plane_rectangles'], 2)
        self.assertEqual(row['consecutive_order_rectangles'], 3)
        self.assertEqual(row['consecutive_same_material_rectangles'], 4)
        self.assertEqual(row['overlapping_same_direction_cells'], 0)


if __name__ == '__main__':
    unittest.main()
