"""The early-game world contract's rules (tools/check_world_contract.py)."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import check_world_contract as contract  # noqa: E402


def report(**changes):
    nearest = dict(water=10, log=6, cobble=7, gravel=10, clay=8, copper=40, tin_gravel=150)
    counts = dict(water=1, log=1, cobble=30000, gravel=30000, clay=30000, copper=9000, tin_gravel=5000)
    for key, value in changes.items():
        group, name = key.split('__')
        (nearest if group == 'near' else counts)[name] = value
    return dict(spawn=[512, 512], nearest=nearest, counts=counts)


class WorldContractTests(unittest.TestCase):
    def test_a_playable_world_passes(self):
        self.assertEqual(contract.check(report()), [])

    def test_each_rule_fails_on_its_own(self):
        self.assertTrue(contract.check(report(near__clay=None)))         # No clay at all
        self.assertTrue(contract.check(report(near__cobble=65)))         # Loose stone too far
        self.assertTrue(contract.check(report(near__tin_gravel=40)))     # Tin by the spawn
        self.assertTrue(contract.check(report(near__tin_gravel=401)))    # Tin out of reach
        self.assertTrue(contract.check(report(count__copper=4999)))      # Too little copper
        self.assertEqual(len(contract.check(report(near__log=40, count__gravel=10))), 2)

    def test_the_published_world_lacks_the_early_materials(self):
        old = ROOT / 'build/checks/large-world-proto/terrain1024-crown-leaves.bin'
        if not old.exists():
            self.skipTest('Prebaked world not present')
        failures = contract.check(contract.scan(old.read_bytes()))
        self.assertTrue(any(f.startswith('clay') for f in failures))


if __name__ == '__main__':
    unittest.main()
