"""Install a new shared executable into a stopped 386 VM, preserving its save.

Copies the old VM to a new private directory, verifies the HDD/CMOS copies,
and switches only the 386 player pointer. The original remains intact.
"""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from fat_image import FatImage, populate
from game_media import build_iso, embedded_boot
from prepare_386_vm import ROOT, OUTPUT, POINTER, EMULATOR, digest, check_bios
from publish_game import atomic_write


def running(vm):
    command = ('Get-CimInstance Win32_Process -Filter "Name = \'86Box.exe\'" | '
        'Select-Object ExecutablePath,CommandLine | ConvertTo-Json -Compress')
    text = subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', command], text=True)
    processes = json.loads(text) if text.strip() else []
    if isinstance(processes, dict):
        processes = [processes]
    return any(str(row.get('ExecutablePath', '')).casefold() == str(EMULATOR).casefold() and
        str(vm).casefold() in str(row.get('CommandLine', '')).casefold() for row in processes)


def main():
    old = Path(POINTER.read_text().strip()).resolve(strict=True)
    if not old.is_relative_to(ROOT / 'runtime'):
        raise ValueError('Expected a private 386 player VM')
    previous = json.loads((old / 'build.json').read_text())
    if previous['vm_directory'] != str(old) or previous['target'] != '386sx25-family-1990' or \
            previous.get('smoke') or previous.get('benchmark') or running(old):
        raise ValueError('Save and close the private 386 player before refreshing it')
    record = json.loads((OUTPUT / 'build.json').read_text())
    exe = (OUTPUT / 'DOSCRAFT.EXE').read_bytes()
    if record['status'] != 'linked' or record.get('benchmark') or digest(exe) != record['exe_sha256']:
        raise ValueError('Expected a freshly linked production executable')
    old_boot = (old / 'boot.img').read_bytes()
    if digest(old_boot) != previous['boot_sha256']:
        raise ValueError('Player boot image differs from its manifest')
    fs = FatImage(old_boot)
    files = [(name, exe if name == 'DOSCRAFT.EXE' else fs.read(name))
        for name, attributes, cluster, size in fs.directory() if not attributes & 16]
    boot = bytes(populate(old_boot[:512], files))
    if FatImage(boot).read('DOSCRAFT.EXE') != exe:
        raise ValueError('Executable boot-media roundtrip failed')
    iso = build_iso(boot, {'DOSCRAFT.EXE': exe,
        'README.TXT': b'DOScraft shared executable: 386 startup Very Low, four-block near distance.\r\n'})
    if embedded_boot(iso) != boot:
        raise ValueError('ISO boot-floppy roundtrip failed')
    disk_hash = digest((old / 'hdd.img').read_bytes())
    cmos = (old / 'nvr/dtk386.nvr').read_bytes()
    check_bios(cmos)
    vm = Path(tempfile.mkdtemp(prefix='game-386-', dir=ROOT / 'runtime'))
    shutil.copytree(old, vm, dirs_exist_ok=True)
    if running(old) or digest((old / 'hdd.img').read_bytes()) != disk_hash or \
            digest((vm / 'hdd.img').read_bytes()) != disk_hash or \
            (old / 'nvr/dtk386.nvr').read_bytes() != cmos or (vm / 'nvr/dtk386.nvr').read_bytes() != cmos:
        raise ValueError('Player disk or CMOS changed during copy; pointer unchanged')
    (vm / 'boot.img').write_bytes(boot)
    (vm / 'DOSCRAFT.ISO').write_bytes(iso)
    updated = {**previous, 'vm_directory': str(vm), 'stage': '386 Very Low startup; player HDD and CMOS preserved',
        'exe_sha256': digest(exe), 'boot_sha256': digest(boot), 'iso_sha256': digest(iso),
        'preserved_player_vm': str(old), 'preserved_hdd_sha256': disk_hash,
        'preserved_cmos_sha256': digest(cmos), 'build_output': str(OUTPUT)}
    updated.pop('boot_validation', None)
    (vm / 'build.json').write_text(json.dumps(updated, indent=2) + '\n')
    if POINTER.read_text().strip() != str(old) or running(old):
        raise ValueError('Player state changed; pointer unchanged')
    atomic_write(POINTER, str(vm).encode('utf-8'))
    print(f'VM={vm}\nPRESERVED_HDD={disk_hash}\nPRESERVED_CMOS={digest(cmos)}')


if __name__ == '__main__':
    main()
