import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from run_vm import validate_vm, main


class LauncherTests(unittest.TestCase):
    def make_vm(self, path):
        (path / '86box.cfg').write_bytes((ROOT / 'profiles/486dx25-platform.cfg').read_bytes())
        record = dict(target='486dx25', probe='display', vm_directory=str(path), exe_sha256='not-used')
        (path / 'build.json').write_text(json.dumps(record))
        return record

    def test_refuses_unknown_target_or_wrong_directory(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name)
            record = self.make_vm(path)
            record['target'] = 'pentium'
            (path / 'build.json').write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, 'primary-target'):
                validate_vm(path)
            record['target'] = '486dx25'
            record['probe'] = 'game'
            (path / 'build.json').write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, 'primary-target'):
                validate_vm(path)
            record['probe'] = 'display'
            record['vm_directory'] = str(path / 'other')
            (path / 'build.json').write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, 'different VM'):
                validate_vm(path)

    def test_refuses_silent_ramdisk_and_hardware_changes_before_media_access(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name)
            self.make_vm(path)
            original = (path / '86box.cfg').read_text()
            for before, after in (('1989_3500rpm', 'ramdisk'), ('25000000', '100000000'),
                                  ('scratch.img', '../unknown.img'), ('16384', '65536')):
                (path / '86box.cfg').write_text(original.replace(before, after))
                with self.assertRaisesRegex(ValueError, 'hardware setting'):
                    validate_vm(path)

    def test_dry_run_never_starts_process(self):
        record = dict(probe='display')
        with patch('run_vm.validate_vm', return_value=(Path('private-vm'), record)), \
             patch('run_vm.launch_command', return_value=['86Box.exe', '-P', 'private-vm']), \
             patch('run_vm.subprocess.Popen') as spawn:
            self.assertEqual(main(['--vm', 'private-vm', '--dry-run']), 0)
            spawn.assert_not_called()


if __name__ == '__main__':
    unittest.main()
