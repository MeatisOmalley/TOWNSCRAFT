import json
import hashlib
import struct
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from run_vm import validate_vm, launch_command, main
from fat_image import populate, scratch_hdd
from build_pc import guest_startup


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

    def test_mouse_media_and_hardware_are_checked(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name)
            record = self.make_vm(path)
            config = (path / '86box.cfg').read_text() + (
                '\n[Input devices]\nmouse_type = msserial\n'
                '\n[Microsoft Serial Mouse]\nport = 0\nbuttons = 2\n'
                '\n[Ports (COM & LPT)]\nserial1_enabled = 1\n')
            executable, driver = b'diagnostic fixture', b'driver fixture'
            digest = hashlib.sha256(driver).hexdigest()
            record.update(probe='mouse', exe_sha256=hashlib.sha256(executable).hexdigest(),
                          mouse_driver=dict(sha256=digest))
            boot = bytearray(512)
            boot[:11] = b'\xeb\x3c\x90DOSCRAFT'
            struct.pack_into('<HBHBHHBH', boot, 11, 512, 1, 1, 2, 224, 2880, 0xf0, 9)
            boot[510:512] = b'\x55\xaa'
            shell = (ROOT / 'guest/FDCONFIG.SYS').read_text().replace('\n', '\r\n').encode('ascii')
            def media(binary=driver, startup=guest_startup('mouse'), shell_config=shell):
                (path / 'boot.img').write_bytes(populate(boot,
                    [('MOUSE.EXE', executable), ('CTMOUSE.EXE', binary),
                     ('AUTOEXEC.BAT', startup), ('FDCONFIG.SYS', shell_config)]))
            (path / '86box.cfg').write_text(config)
            (path / 'build.json').write_text(json.dumps(record))
            (path / 'scratch.img').write_bytes(scratch_hdd())
            media()
            with patch('run_vm.CTMOUSE_EXE_SHA256', digest):
                self.assertEqual(validate_vm(path)[1]['probe'], 'mouse')
                for before, after in (('msserial', 'ps2'), ('port = 0', 'port = 1'),
                                      ('buttons = 2', 'buttons = 4'),
                                      ('serial1_enabled = 1', 'serial1_enabled = 0')):
                    (path / '86box.cfg').write_text(config.replace(before, after))
                    with self.assertRaisesRegex(ValueError, 'hardware setting'):
                        validate_vm(path)
                (path / '86box.cfg').write_text(config)
                for extra in ('serial1_device = serial_passthrough\n', 'serial1_device = pipe\n',
                              'serial2_device = pipe\n', 'serial1_passthrough_enabled = 1\n',
                              '\n[Serial Passthrough #1]\nmode = 3\n',
                              '\n[Named Pipe (COM) #1]\npath = example\n'):
                    (path / '86box.cfg').write_text(config + extra)
                    with self.assertRaisesRegex(ValueError, 'hardware setting'):
                        validate_vm(path)
                (path / '86box.cfg').write_text(config)
                media(binary=b'wrong driver')
                with self.assertRaisesRegex(ValueError, 'driver does not match'):
                    validate_vm(path)
                startup = guest_startup('mouse')
                for altered in (startup.replace(b'/R11 ', b''),
                                startup.replace(b'CTMOUSE /S14', b'REM CTMOUSE /S14'),
                                startup + b'CTMOUSE /R99\r\n',
                                startup.replace(b'MOUSE.EXE /AUTO\r\n', b'')):
                    media(startup=altered)
                    with self.assertRaisesRegex(ValueError, 'startup sequence'):
                        validate_vm(path)
                media(shell_config=shell.replace(b'AUTOEXEC.BAT', b'OTHER.BAT'))
                with self.assertRaisesRegex(ValueError, 'shell configuration'):
                    validate_vm(path)
                media()
                record['mouse_driver']['sha256'] = 'wrong manifest hash'
                (path / 'build.json').write_text(json.dumps(record))
                with self.assertRaisesRegex(ValueError, 'driver does not match'):
                    validate_vm(path)

    def test_replaced_emulator_is_rejected_before_launch(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name)
            (path / 'build/deps/86box').mkdir(parents=True)
            (path / 'build/deps/86box/86Box.exe').write_bytes(b'replaced emulator')
            with patch('run_vm.ROOT', path):
                with self.assertRaisesRegex(ValueError, 'executable checksum mismatch'):
                    launch_command(path)


if __name__ == '__main__':
    unittest.main()
