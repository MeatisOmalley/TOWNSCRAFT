"""Clone a stopped player disk into an independent DX2/80, W32 VLB SCSI VM."""
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

from build_game import ROOT, OUTPUT
from fetch_roms import PATHS
from publish_game import running

PROFILE = ROOT / 'profiles/1992-dx2-80.cfg'
EMULATOR = ROOT / 'build/deps/86box-doscraft-1992/86Box.exe'
ROMS = ROOT / 'build/deps/86box/roms'
POINTER = OUTPUT / 'latest-1992-vm.txt'
REQUIRED_ROMS = (
    'machines/av4/amibios_486dx_isa_bios_aa4025963.bin',
    'video/et4000w32/ET4000W32VLB_bios_MX27C512.BIN',
    'scsi/adaptec/aha1542c102.bin',
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    source = Path((OUTPUT / 'latest-play-vm.txt').read_text().strip()).resolve(strict=True)
    if not source.is_relative_to(ROOT / 'runtime'):
        raise ValueError('Source must be a private DOScraft player VM')
    record = json.loads((source / 'build.json').read_text())
    if record.get('smoke') or record.get('benchmark') or record.get('target') != '486dx25':
        raise ValueError('Source is not the playable game')
    if running(source):
        raise ValueError('Save and close the current player VM before copying its disk')
    if digest(source / 'DOSCRAFT.ISO') != record['iso_sha256']:
        raise ValueError('Source ISO differs from manifest')
    if digest(source / 'boot.img') != record['boot_sha256']:
        raise ValueError('Source boot disk differs from manifest')
    large = bool(record.get('large_world'))
    disk_name = 'scratch-large.img' if large else 'scratch.img'
    disk_bytes = 206438400 if large else 21411840
    if (source / disk_name).stat().st_size != disk_bytes:
        raise ValueError('Source disk geometry changed')
    if not EMULATOR.is_file():
        raise ValueError('Build the 1992-patched emulator first')
    for name in REQUIRED_ROMS:
        if digest(ROMS / name) != PATHS[name]:
            raise ValueError('Missing or changed ROM: ' + name)
    original_disk_hash = digest(source / disk_name)
    vm = Path(tempfile.mkdtemp(prefix='game-1992-', dir=ROOT / 'runtime'))
    for name in ('boot.img', disk_name):
        shutil.copy2(source / name, vm / name)
    shutil.copy2(PROFILE, vm / '86box.cfg')
    if large:
        config=(vm / '86box.cfg').read_text().replace(
            'hdd_01_parameters = 17, 4, 615, 0, scsi\n','hdd_01_parameters = 63, 8, 800, 0, scsi\n').replace(
            'hdd_01_fn = scratch.img\n','hdd_01_fn = scratch-large.img\n')
        (vm / '86box.cfg').write_text(config)
    if (running(source) or digest(source / disk_name) != original_disk_hash
            or digest(vm / disk_name) != original_disk_hash):
        raise ValueError('Source player disk changed during copy; VM not published')
    updated = {**record,
        'stage': 'Separate 1992 hardware VM; interactive acceptance pending',
        'target': 'dx2-80-1992', 'vm_directory': str(vm),
        'profile_sha256': digest(PROFILE),
        'emulator_sha256': digest(EMULATOR),
        'rom_sha256': {name: PATHS[name] for name in REQUIRED_ROMS},
        'preserved_player_vm': str(source),
        'preserved_hdd_sha256': original_disk_hash,
        'disk_note': ('Private 206 MB FAT16 prebaked 1024-world copy' if large else 'Private 20 MB FAT16 copy') +
            '; Quantum ProDrive 1050S seek, rotation and 512 KiB read-cache timing proxy, not full 1.05 GB capacity',
        'hardware_era': '1992', 'shared_iso_path': str(ROOT / 'ISO/DOSCRAFT.ISO'),
        'cache_note': '86Box timing approximation; no 256 KB cache capacity or 12 ns SRAM model',
        'bus_note': 'ET4000/W32 on VLB; AHA-1542C on 16-bit ISA (EISA unavailable)',
    }
    updated.pop('bios_seed_sha256', None)  # The source CMOS belongs to a different motherboard.
    (vm / 'build.json').write_text(json.dumps(updated, indent=2) + '\n')
    POINTER.write_text(str(vm))
    print(f'VM={vm}\nSOURCE={source}\nHDD_SHA256={original_disk_hash}')


if __name__ == '__main__':
    main()
