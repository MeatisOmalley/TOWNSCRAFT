"""1996 VM media and hardware profile (tools/vm_1996.py)."""
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

import vm_1996  # noqa: E402
from fat_image import FatImage  # noqa: E402
from iso_image import build_iso  # noqa: E402

FREEDOS = ROOT / 'build/deps/freedos/144m/x86BOOT.img'


class ProfileTests(unittest.TestCase):
    def test_both_configurations_differ_only_in_the_voodoo(self):
        plain, voodoo = vm_1996.config_text(False), vm_1996.config_text(True)
        self.assertEqual(plain.replace('voodoo = 0\n', 'voodoo = 1\n'), voodoo)
        with tempfile.TemporaryDirectory() as folder:
            for enabled, text in ((False, plain), (True, voodoo)):
                path = Path(folder) / vm_1996.CONFIGS[enabled]
                path.write_text(text)
                vm_1996.check_config(path, enabled)
                with self.assertRaises(ValueError):
                    vm_1996.check_config(path, not enabled)

    def test_profile_refuses_changed_hardware(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / '86box.cfg'
            path.write_text(vm_1996.config_text(False).replace('cpu_speed = 200000000', 'cpu_speed = 166666666'))
            with self.assertRaises(ValueError):
                vm_1996.check_config(path, False)
            path.write_text(vm_1996.config_text(False).replace('cpu_use_dynarec = 1', 'cpu_use_dynarec = 0'))
            with self.assertRaises(ValueError):
                vm_1996.check_config(path, False)

    def test_every_required_rom_is_pinned(self):
        from fetch_roms import PATHS
        for name in vm_1996.REQUIRED_ROMS:
            self.assertEqual(len(PATHS[name]), 64)


class StartupTests(unittest.TestCase):
    def test_play_configures_sound_and_falls_back_to_software(self):
        text = vm_1996.startup('play').decode('ascii')
        lines = text.split('\r\n')
        self.assertLess(lines.index('AWECFG'), lines.index('D963DFX'))
        self.assertEqual(lines[lines.index('D963DFX') + 1], 'IF ERRORLEVEL 1 D96SOFT')
        self.assertIn('SET BLASTER=A220 I7 D1 H5 P330 T6', lines)

    def test_smoke_runs_all_three_and_keeps_their_reports(self):
        text = vm_1996.startup('smoke').decode('ascii')
        for exe, tag in (('D96REF', 'REF'), ('D96SOFT', 'SOFT'), ('D963DFX', '3DFX')):
            self.assertIn(f'{exe} /SMOKE\r\n{exe} /SMOKELOAD\r\n', text)
            self.assertIn(f'C:\\{tag}NEW.TXT', text)
            self.assertIn(f'C:\\{tag}LOAD.TXT', text)
        self.assertTrue(text.endswith('ECHO PASS> C:\\SMOKE.END\r\nECHO Smoke finished.\r\n'))

    def test_benchmark_modes(self):
        self.assertIn('D96SOFT /SWEEP\r\n', vm_1996.startup('bench', 'soft', 'sweep').decode('ascii'))
        separate = vm_1996.startup('bench', '3dfx', 'separate').decode('ascii')
        for n in (1, 2, 3):
            self.assertIn(f'D963DFX /{n}\r\n', separate)


@unittest.skipUnless(FREEDOS.is_file(), 'FreeDOS boot floppy dependency not fetched')
class MediaTests(unittest.TestCase):
    def test_ed_floppy_holds_three_executables(self):
        payload = {name: bytes([n]) * 600000 for n, name in enumerate(('D96REF.EXE', 'D96SOFT.EXE', 'D963DFX.EXE'), 1)}
        image = vm_1996.ed_floppy([('KERNEL.SYS', b'k'), *sorted(payload.items())])
        self.assertEqual(len(image), 2880 * 1024)
        floppy = FatImage(image)
        self.assertEqual((floppy.bits, floppy.total, floppy.spc), (12, 5760, 2))
        for name, data in payload.items():
            self.assertEqual(floppy.read(name), data)

    def test_embedded_boot_roundtrip(self):
        image = vm_1996.ed_floppy([('A.TXT', b'a')])
        iso = build_iso(image, {'A.TXT': b'a'}, volume_id='DOSCRAFT_1996')
        self.assertEqual(vm_1996.embedded_boot(iso), image)
        with self.assertRaises(ValueError):
            vm_1996.embedded_boot(build_iso(image[:1440 * 1024], {}))


if __name__ == '__main__':
    unittest.main()
