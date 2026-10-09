"""Validate and launch one explicitly prepared diagnostic VM (never a game).

No downloads, compilation, image creation/replacement or formatting. --dry-run
validates the same machine/media contract without starting the emulator.
"""
import argparse
import configparser
import hashlib
import json
import re
import struct
from pathlib import Path
import subprocess
import sys
from fat_image import FatImage
from fetch_roms import PATHS
from fetch_dependencies import CTMOUSE_EXE_SHA256, EMULATOR_EXE_SHA256
from build_pc import guest_startup

ROOT = Path(__file__).resolve().parents[1]


def diagnostic_volume(disk):
    """Require the generated single FAT16 C: partition, not an arbitrary offset."""
    total, start = 615 * 4 * 17, 17
    size = total - start
    entry = bytes([0,1,1,0,6,3,17 | ((614 >> 2) & 0xc0),614 & 255]) + struct.pack('<II',start,size)
    if (len(disk) != total * 512 or disk[510:512] != b'\x55\xaa' or
            disk[446:462] != entry or any(disk[462:510])):
        raise ValueError('Diagnostic HDD partition mismatch')
    boot = disk[start*512:(start+1)*512]
    if (boot[510:512] != b'\x55\xaa' or
            struct.unpack_from('<HBHBHHBH',boot,11) != (512,4,1,2,512,size,0xf8,41) or
            struct.unpack_from('<HHII',boot,24) != (17,4,start,0)):
        raise ValueError('Diagnostic HDD BPB mismatch')
    volume = FatImage(disk,start*512)
    if volume.bits != 16 or volume.total != size:
        raise ValueError('Diagnostic HDD FAT16 extent mismatch')
    return volume


def validate_vm(vm):
    vm = Path(vm).resolve(strict=True)
    record = json.loads((vm / 'build.json').read_text())
    if record.get('target') != '486dx25' or record.get('probe') not in ('platform', 'display', 'adapter', 'system', 'mouse', 'storage'):
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
    if record['probe'] == 'mouse':
        required.update({'Input devices': {'mouse_type': 'msserial'},
                         'Microsoft Serial Mouse': {'port': '0', 'buttons': '2'}})
        # 86Box omits default-enabled COM1 when rewriting config.
        if cfg.getint('Ports (COM & LPT)', 'serial1_enabled', fallback=1) != 1:
            raise ValueError('Unexpected hardware setting: COM1 disabled')
        for key, value in cfg.items('Ports (COM & LPT)') if cfg.has_section('Ports (COM & LPT)') else ():
            if ((re.fullmatch(r'serial\d+_device', key) and value != 'none') or
                    (re.fullmatch(r'serial\d+_passthrough_enabled', key) and int(value))):
                raise ValueError('Unexpected hardware setting: external serial device')
        for section in cfg.sections():
            if section.startswith(('Serial Passthrough', 'Named Pipe (COM)', 'Virtual Console (COM)')):
                raise ValueError('Unexpected hardware setting: serial passthrough section')
    for section, values in required.items():
        for key, value in values.items():
            if cfg.get(section, key, fallback=None) != value:
                raise ValueError(f'Unexpected hardware setting {section}/{key}; refusing launch')
    # Default-off setting is omitted when 86Box rewrites its config.
    if cfg.getint('Machine', 'cpu_override_interpreter', fallback=0):
        raise ValueError('Inaccurate interpreter override is not a benchmark profile')
    program = record['probe'].upper() + '.EXE'
    media = FatImage((vm / 'boot.img').read_bytes())
    payload = media.read(program)
    if hashlib.sha256(payload).hexdigest() != record['exe_sha256']:
        raise ValueError('Boot media executable does not match its manifest')
    if record['probe'] == 'mouse':
        if (hashlib.sha256(media.read('CTMOUSE.EXE')).hexdigest() != CTMOUSE_EXE_SHA256 or
                record['mouse_driver']['sha256'] != CTMOUSE_EXE_SHA256):
            raise ValueError('Mouse driver does not match its manifest')
    if record['probe'] in ('mouse', 'storage'):
        if media.read('AUTOEXEC.BAT') != guest_startup(record['probe']):
            raise ValueError('Diagnostic startup sequence does not match the prepared probe')
        expected_config = (ROOT / 'guest/FDCONFIG.SYS').read_text().replace('\n', '\r\n').encode('ascii')
        if media.read('FDCONFIG.SYS') != expected_config:
            raise ValueError('DOS shell configuration does not match the diagnostic')
    scratch = vm / 'scratch.img'
    if scratch.stat().st_size != 615 * 4 * 17 * 512:
        raise ValueError('Scratch HDD size does not match its declared geometry')
    if record['probe'] == 'storage':
        from terrain_fixture import terrain_image
        disk = scratch.read_bytes()
        terrain = diagnostic_volume(disk).read('TERRAIN.TMP')
        expected = terrain_image()
        # Payload legitimately changes after this diagnostic. The exact marker
        # and capacity must still match; never format an unknown file at launch.
        if len(terrain) != len(expected) or terrain[:512] != expected[:512]:
            raise ValueError('Unexpected terrain file marker/capacity')
        if record['terrain_fixture_sha256'] != hashlib.sha256(expected).hexdigest():
            raise ValueError('Terrain fixture does not match its manifest')
    return vm, record


def launch_command(vm):
    executable = ROOT / 'build/deps/86box/86Box.exe'
    roms = ROOT / 'build/deps/86box/roms'
    if not executable.is_file():
        raise FileNotFoundError(f'86Box missing: {executable}')
    if hashlib.sha256(executable.read_bytes()).hexdigest() != EMULATOR_EXE_SHA256:
        raise ValueError('86Box executable checksum mismatch')
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
