"""Configuration/build contract tests; not a substitute for guest boot tests."""
import configparser
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_pc import TARGETS, argument_parser, compiler_flags
from fetch_roms import PATHS


def profile(target):
    result = configparser.ConfigParser()
    result.read(ROOT / 'profiles' / f'{target}-platform.cfg')
    return result


class TargetTests(unittest.TestCase):
    def test_default_is_native_486_with_x87(self):
        self.assertEqual(argument_parser().parse_args([]).target, '486dx25')
        self.assertEqual(TARGETS['486dx25']['role'], 'primary')
        flags = compiler_flags('486dx25')
        self.assertIn('-march=i486', flags)
        self.assertIn('-mtune=i486', flags)
        self.assertIn('-mfpmath=387', flags)
        self.assertIn('-m80387', flags)
        self.assertNotIn('-ffast-math', flags)
        cfg = profile('486dx25')['Machine']
        self.assertEqual(cfg['machine'], 'isa486')
        self.assertEqual(cfg['cpu_family'], 'i486dx')
        self.assertEqual(cfg.getint('cpu_speed'), 25000000)
        self.assertEqual(cfg['fpu_type'], 'internal')
        self.assertEqual(cfg.getint('cpu_use_dynarec'), 0)

    def test_legacy_386_is_not_a_current_game_gate_or_fake_cyrix(self):
        self.assertEqual(TARGETS['386dx33']['role'], 'deferred_compatibility')
        self.assertIn('-march=i386', compiler_flags('386dx33'))
        cfg = profile('386dx33')['Machine']
        self.assertEqual(cfg['cpu_family'], 'i386dx')
        self.assertEqual(cfg.getint('cpu_speed'), 33333333)
        self.assertEqual(cfg['fpu_type'], '387')
        self.assertEqual(cfg.getint('cpu_override_interpreter'), 0)

    def test_shared_peripherals_and_ram(self):
        primary, legacy = profile('486dx25'), profile('386dx33')
        for section in primary.sections():
            if section != 'Machine':
                self.assertEqual(dict(primary[section]), dict(legacy[section]), section)
        self.assertEqual(primary['Machine'].getint('mem_size'), 16384)
        self.assertEqual(primary['Machine']['mem_size'], legacy['Machine']['mem_size'])
        self.assertEqual(primary['Video']['gfxcard'], 'et4000ax')
        self.assertEqual(primary['Tseng Labs ET4000AX (ISA)'].getint('memory'), 1024)

    def test_required_roms_are_pinned(self):
        for name in ('machines/isa486/ISA-486.BIN', 'machines/asus386/ASUS_ISA-386C_BIOS.bin',
                     'video/et4000/ET4000_V8_06.BIN', 'hdd/esdi_at/62-000279-061.bin'):
            self.assertEqual(len(PATHS[name]), 64)
            self.assertEqual(len(bytes.fromhex(PATHS[name])), 32)


if __name__ == '__main__': unittest.main()
