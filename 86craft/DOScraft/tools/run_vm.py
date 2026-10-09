"""Validate and launch one explicitly prepared diagnostic VM (never a game).

No downloads, compilation, image creation/replacement or formatting. --dry-run
validates the same machine/media contract without starting the emulator.
"""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from fat_image import FatImage
from fetch_roms import PATHS

ROOT = Path(__file__).resolve().parents[1]


def validate_vm(vm):
    vm = Path(vm).resolve(strict=True)
    record = json.loads((vm / 'build.json').read_text())
    if record.get('target') != '486dx25' or record.get('probe') not in ('platform', 'display', 'adapter', 'system'):
        raise ValueError('Only prepared primary-target diagnostic VMs are supported')
    if Path(record['vm_directory']).resolve() != vm:
        raise ValueError('Manifest names a different VM directory')
    cfg = configparser.ConfigParser()
    cfg.read(vm / '86box.cfg', encoding='utf-8-sig')
    required = {
        'Machine': {'machine': 'isa486', 'cpu_family': 'i486dx', 'cpu_speed': '25000000',
                    'cpu_use_dynarec': '0', 'fpu_type': 'internal', 'mem_size': '16384'},
        'Video': {'gfxcard': 'et4000ax'},
        'Tseng Labs ET4000AX (ISA)': {'bios': 'v8_06', 'memory': '1024'},
        'Sound': {'sndcard': 'sb'},
        'Storage controllers': {'hdc_1': 'esdi_at'},
        'Hard disks': {'hdd_01_fn': 'scratch.img', 'hdd_01_speed': '1989_3500rpm',
                       'hdd_01_parameters': '17, 4, 615, 0, esdi'},
        'Floppy and CD-ROM drives': {'fdd_01_fn': 'boot.img', 'fdd_01_type': '35_2hd'},
    }
    for section, values in required.items():
        for key, value in values.items():
            if cfg.get(section, key, fallback=None) != value:
                raise ValueError(f'Unexpected hardware setting {section}/{key}; refusing launch')
    # Default-off setting is omitted when 86Box rewrites its config.
    if cfg.getint('Machine', 'cpu_override_interpreter', fallback=0):
        raise ValueError('Inaccurate interpreter override is not a benchmark profile')
    program = record['probe'].upper() + '.EXE'
    payload = FatImage((vm / 'boot.img').read_bytes()).read(program)
    if hashlib.sha256(payload).hexdigest() != record['exe_sha256']:
        raise ValueError('Boot media executable does not match its manifest')
    scratch = vm / 'scratch.img'
    if scratch.stat().st_size != 615 * 4 * 17 * 512:
        raise ValueError('Scratch HDD size does not match its declared geometry')
    return vm, record


def launch_command(vm):
    executable = ROOT / 'build/deps/86box/86Box.exe'
    roms = ROOT / 'build/deps/86box/roms'
    if not executable.is_file():
        raise FileNotFoundError(f'86Box missing: {executable}')
    for relative in ('machines/isa486/ISA-486.BIN', 'video/et4000/ET4000_V8_06.BIN',
                     'hdd/esdi_at/62-000279-061.bin'):
        if hashlib.sha256((roms / relative).read_bytes()).hexdigest() != PATHS[relative]:
            raise ValueError(f'ROM checksum mismatch: {relative}')
    return [str(executable), '-P', str(vm), '-R', str(roms)]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vm', required=True, type=Path, help='Exact directory printed by build_pc.py')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args(argv)
    try:
        vm, record = validate_vm(args.vm)
        command = launch_command(vm)
        print(f'{record["probe"]} diagnostic only; not a playable game. VM: {vm}')
        print('Disk: 20 MiB scratch, generic 1989 timing proxy; not final Wren V.')
        print(json.dumps(command))
        if not args.dry_run:
            subprocess.Popen(command, cwd=vm)
    except (OSError, ValueError, KeyError, configparser.Error) as error:
        print(f'VM launch refused: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
