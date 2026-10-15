"""Copy the stopped player VM to June 1990 hardware without touching its save."""
import hashlib
import argparse
import json
from pathlib import Path
import shutil
import tempfile

from build_game import ROOT, OUTPUT
from fetch_roms import PATHS
from publish_game import running

PROFILE = ROOT / 'profiles/1990-dx40.cfg'
EMULATOR = ROOT / 'build/deps/86box-doscraft-1990/86Box.exe'
POINTER = OUTPUT / 'latest-play-vm.txt'
ROM_ROOT = ROOT / 'build/deps/86box/roms'
REQUIRED_ROMS = (
    'machines/isa486/ISA-486.BIN',
    'video/et4000/ET4000_V8_06.BIN',
    'scsi/adaptec/aha1540a307.bin',
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def seeded_cmos(data):
    """Keep the known-good same-board settings; update HDD/8 MiB fields and checksum."""
    if len(data) != 128 or data[0x12] != 0x20 or int.from_bytes(data[0x17:0x19], 'little') != 15360:
        raise ValueError('Player CMOS is not the expected 16 MiB ESDI seed')
    if sum(data[0x10:0x2e]) & 0xffff != int.from_bytes(data[0x2e:0x30], 'big'):
        raise ValueError('Player CMOS checksum is invalid')
    out = bytearray(data)
    out[0x12] = 0  # Motherboard HDDs None; option-ROM SCSI owns C:.
    out[0x17:0x19] = (7168).to_bytes(2, 'little')
    out[0x30:0x32] = (7168).to_bytes(2, 'little')
    out[0x2e:0x30] = (sum(out[0x10:0x2e]) & 0xffff).to_bytes(2, 'big')
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-only', action='store_true', help='Build a private VM without switching the player launcher')
    args = parser.parse_args()
    source = Path(POINTER.read_text().strip()).resolve(strict=True)
    if not source.is_relative_to(ROOT / 'runtime'):
        raise ValueError('Player source must be a private DOScraft VM')
    record = json.loads((source / 'build.json').read_text())
    if record.get('smoke') or record.get('benchmark') or record.get('target') != '486dx25':
        raise ValueError('Source is not the current player VM')
    if running(source):
        raise ValueError('Save and close the player VM before upgrading')
    if digest(source / 'DOSCRAFT.ISO') != record['iso_sha256'] or digest(source / 'boot.img') != record['boot_sha256']:
        raise ValueError('Player media differs from its manifest')
    if (source / 'scratch.img').stat().st_size != 21411840:
        raise ValueError('Unexpected player disk geometry')
    if not EMULATOR.is_file():
        raise ValueError('The June 1990 patched emulator is missing')
    for name in REQUIRED_ROMS:
        if digest(ROM_ROOT / name) != PATHS[name]:
            raise ValueError('Missing or changed ROM: ' + name)
    cmos = seeded_cmos((source / 'nvr/isa486.nvr').read_bytes())
    original_disk_hash = digest(source / 'scratch.img')
    vm = Path(tempfile.mkdtemp(prefix='game-1990-', dir=ROOT / 'runtime'))
    for name in ('DOSCRAFT.ISO', 'boot.img', 'scratch.img'):
        shutil.copy2(source / name, vm / name)
    shutil.copy2(PROFILE, vm / '86box.cfg')
    (vm / 'nvr').mkdir()
    (vm / 'nvr/isa486.nvr').write_bytes(cmos)
    if running(source) or digest(source / 'scratch.img') != original_disk_hash or digest(vm / 'scratch.img') != original_disk_hash:
        raise ValueError('Player disk changed while copying; new VM not published')
    updated = {**record,
        'stage': 'June 1990 hardware upgrade; interactive acceptance pending',
        'vm_directory': str(vm),
        'profile_sha256': digest(PROFILE),
        'bios_seed_sha256': hashlib.sha256(cmos).hexdigest(),
        'emulator_sha256': digest(EMULATOR),
        'rom_sha256': {name: PATHS[name] for name in REQUIRED_ROMS},
        'preserved_player_vm': str(source),
        'preserved_hdd_sha256': original_disk_hash,
        'hardware_era': '1990-06-30',
        'disk_note': 'Independent 20 MiB FAT16 copy; ST4385N Wren Runner timing proxy, not full 330 MB capacity',
        'cache_note': '256 KiB 20 ns SRAM target; private emulator forces SiS 401 external cache enabled with 2-cycle cache and 6-cycle DRAM access proxies; 86Box does not model L2 capacity/hit rate',
        'ram_note': '8 MiB fast page-mode DRAM; 86Box has no per-SIMM nanosecond setting',
    }
    (vm / 'build.json').write_text(json.dumps(updated, indent=2) + '\n')
    if not args.prepare_only:
        POINTER.write_text(str(vm))
    print(f'VM={vm}\nSOURCE={source}\nHDD_SHA256={original_disk_hash}')


if __name__ == '__main__':
    main()
