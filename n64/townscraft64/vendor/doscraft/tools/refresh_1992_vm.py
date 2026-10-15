"""Copy the stopped 1992 VM onto the shared ISO's extracted boot floppy.

The 1992 machine boots A: rather than the ISO directly. Preserve its writable
HDD and CMOS in a new VM; never edit the existing player checkout in place.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile

from build_game import ROOT, OUTPUT
from fat_image import FatImage, populate, scratch_hdd
from game_media import embedded_boot, large_volume, startup
from publish_game import atomic_write
from save_fixture import save_image
from large_world_media import check_terrain, upgrade_disk,identity,FAR_BYTES

ISO = ROOT / 'ISO/DOSCRAFT.ISO'
POINTER = OUTPUT / 'latest-1992-vm.txt'
PROFILE = ROOT / 'profiles/1992-dx2-80.cfg'
EMULATOR = ROOT / 'build/deps/86box-doscraft-1992/86Box.exe'
ROMS = ROOT / 'build/deps/86box/roms'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    return sha(path.read_bytes())


def running(vm):
    command = ('$env:PSModulePath=(Join-Path $PSHOME \'Modules\')+\';\'+$env:PSModulePath; '
               'Get-CimInstance Win32_Process -Filter "Name = \'86Box.exe\'" | '
               'Select-Object ExecutablePath,CommandLine | ConvertTo-Json -Compress')
    result = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-Command', command],
                            check=True, capture_output=True, text=True)
    processes = json.loads(result.stdout) if result.stdout.strip() else []
    if isinstance(processes, dict):
        processes = [processes]
    return any(str(p.get('ExecutablePath', '')).casefold() == str(EMULATOR).casefold()
               and str(vm).casefold() in str(p.get('CommandLine', '')).casefold()
               for p in processes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--large-terrain', type=Path,
                        help='Install the published prebaked 1024 seed on a new private HDD; smoke-test before switching')
    parser.add_argument('--appendix-only',action='store_true',
                        help='Copy a compatible distant appendix into an existing large-world VM; preserve hardware/CMOS, use the shared 1990 smoke evidence')
    args = parser.parse_args()
    if args.appendix_only and not args.large_terrain:
        parser.error('--appendix-only requires --large-terrain')
    old = Path(POINTER.read_text().strip()).resolve(strict=True)
    if not old.is_relative_to(ROOT / 'runtime') or running(old):
        raise ValueError('Current 1992 VM must be a stopped private VM')
    record = json.loads((old / 'build.json').read_text())
    if record.get('target') != 'dx2-80-1992' or record.get('smoke') or record.get('benchmark'):
        raise ValueError('Current VM is not the 1992 playable profile')
    if file_sha(EMULATOR) != record['emulator_sha256'] or file_sha(PROFILE) != record['profile_sha256']:
        raise ValueError('1992 emulator or hardware profile changed')
    for name, expected in record['rom_sha256'].items():
        if file_sha(ROMS / name) != expected:
            raise ValueError('1992 ROM changed: ' + name)
    disk_name = 'scratch-large.img' if record.get('large_world') else 'scratch.img'
    disk_bytes = 206438400 if record.get('large_world') else 21411840
    if file_sha(old / 'boot.img') != record['boot_sha256'] or \
       (old / disk_name).stat().st_size != disk_bytes or \
       (old / 'nvr/av4.nvr').stat().st_size != 128:
        raise ValueError('Current 1992 boot, HDD or CMOS differs from its manifest')

    iso = ISO.read_bytes()
    boot = embedded_boot(iso)
    exe_hash = sha(FatImage(boot).read('DOSCRAFT.EXE'))
    current_player = Path((OUTPUT / 'latest-play-vm.txt').read_text().strip()).resolve(strict=True)
    if not current_player.is_relative_to(ROOT / 'runtime'):
        raise ValueError('Current June 1990 player VM is outside runtime')
    current_record = json.loads((current_player / 'build.json').read_text())
    if (current_record.get('smoke') or current_record.get('benchmark') or
        current_record['iso_sha256'] != sha(iso) or
        current_record['exe_sha256'] != exe_hash):
        raise ValueError('Shared ISO does not match the current June 1990 player')
    terrain = None
    if args.large_terrain:
        terrain = args.large_terrain.read_bytes()
        check_terrain(terrain)
        if (not current_record.get('large_world') or sha(terrain) != current_record['terrain_sha256']):
            raise ValueError('Terrain is not the prebaked seed in the published 1024 release')
    if args.appendix_only:
        if not record.get('large_world') or len(terrain)!=FAR_BYTES:
            raise ValueError('Appendix-only refresh requires existing large terrain and the distant appendix')
        old_terrain=large_volume((old/disk_name).read_bytes()).read('TERRAIN.TMP')
        if identity(old_terrain)!=identity(terrain):
            raise ValueError('Appendix-only refresh cannot change seed identity')
        evidence=json.loads((Path(current_record['verified_smoke_vm'])/'large-world-smoke.json').read_text())
        if any('RESULT=PASS\n' not in evidence.get(mode,'') for mode in ('new','load')):
            raise ValueError('Shared release smoke evidence is missing')
    disk_hash = file_sha(old / disk_name)
    vm = Path(tempfile.mkdtemp(prefix='game-1992-', dir=ROOT / 'runtime'))
    (vm / 'boot.img').write_bytes(boot)
    if terrain is None:
        shutil.copy2(old / disk_name, vm / disk_name)
    else:
        if record.get('large_world'):
            disk = upgrade_disk((old / disk_name).read_bytes(),terrain)
        else:
            disk = scratch_hdd(cylinders=800, heads=8, sectors=63)
            start = struct.unpack_from('<I', disk, 454)[0] * 512
            disk[start:] = populate(disk[start:start + 512],
                                    [('TERRAIN.TMP', terrain), ('WORLD.SAV', save_image())])
            if large_volume(disk).read('TERRAIN.TMP') != terrain:
                raise ValueError('New HDD terrain verification failed')
        with (vm / 'scratch-large.img').open('xb') as file:
            file.write(disk)
        del disk
    shutil.copy2(old / '86box.cfg', vm / '86box.cfg')
    (vm / 'nvr').mkdir()
    shutil.copy2(old / 'nvr/av4.nvr', vm / 'nvr/av4.nvr')
    if terrain is not None and not record.get('large_world'):
        config_path = vm / '86box.cfg'
        config = config_path.read_text()
        before = 'hdd_01_parameters = 17, 4, 615, 0, scsi\n'
        if config.count(before) != 1 or config.count('hdd_01_fn = scratch.img\n') != 1:
            raise ValueError('Unexpected 1992 disk configuration')
        config_path.write_text(config.replace(before, 'hdd_01_parameters = 63, 8, 800, 0, scsi\n')
                              .replace('hdd_01_fn = scratch.img\n', 'hdd_01_fn = scratch-large.img\n'))
    if running(old) or file_sha(old / disk_name) != disk_hash or \
       (terrain is None and file_sha(vm / disk_name) != disk_hash):
        raise ValueError('1992 disk changed during copy; pointer not switched')
    updated = {**record,
               'stage': '1992 hardware with shared DOScraft ISO; interactive acceptance pending',
               'vm_directory': str(vm), 'exe_sha256': exe_hash,
               'iso_sha256': sha(iso), 'boot_sha256': sha(boot),
               'preserved_player_vm': str(old), 'preserved_hdd_sha256': disk_hash,
               'shared_iso_path': str(ISO)}
    if terrain is not None:
        updated.update(large_world=True, large_world_experiment=True,
                       terrain_sha256=sha(terrain), terrain_header_sha256=sha(terrain[:33 * 512]),
                       disk_note='Private 206 MB SCSI disk with prebaked 1024 seed; Quantum ProDrive 1050S timing')
        updated['preserved_world_edits']=bool(record.get('large_world'))
    (vm / 'build.json').write_text(json.dumps(updated, indent=2) + '\n')
    if terrain is not None and not args.appendix_only:
        # Test a separate copy, leaving the new player's seed and save medium clean.
        smoke = Path(tempfile.mkdtemp(prefix='game-smoke-1992-', dir=ROOT / 'runtime'))
        for name in ('scratch-large.img', '86box.cfg'):
            shutil.copy2(vm / name, smoke / name)
        shutil.copytree(vm / 'nvr', smoke / 'nvr')
        files = FatImage(boot)
        smoke_boot = populate(boot[:512], [(name, startup(smoke=True) if name == 'AUTOEXEC.BAT'
                               else files.read(name)) for name, attr, cluster, size in files.directory()])
        (smoke / 'boot.img').write_bytes(smoke_boot)
        smoke_record = {**updated, 'smoke': True, 'vm_directory': str(smoke),
                        'boot_sha256': sha(smoke_boot)}
        (smoke / 'build.json').write_text(json.dumps(smoke_record, indent=2) + '\n')
        subprocess.run([sys.executable, str(ROOT / 'tools/large_game_smoke.py'), '--vm', str(smoke)], check=True)
        updated['verified_smoke_vm'] = str(smoke)
        (vm / 'build.json').write_text(json.dumps(updated, indent=2) + '\n')
    if args.appendix_only:
        updated['appendix_validation']='Compatible seed identity, native migration and June 1990 shared executable smoke; no new 1992 run'
        (vm/'build.json').write_text(json.dumps(updated,indent=2)+'\n')
    if (Path(POINTER.read_text().strip()).resolve() != old or file_sha(ISO) != sha(iso) or
        running(old) or file_sha(old / disk_name) != disk_hash):
        raise ValueError('1992 pointer or shared ISO changed concurrently')
    atomic_write(POINTER, str(vm).encode('utf-8'))
    print(f'VM={vm}\nSOURCE={old}\nISO={ISO}\nPRESERVED_HDD={disk_hash}')


if __name__ == '__main__':
    main()
