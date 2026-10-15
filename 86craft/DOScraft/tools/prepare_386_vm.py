"""Prepare a new private 386SX-25 VM; never replace a player disk.

Uses the shared 386/486 game executable and a fresh V2 compact world on the
profile's 40 MB IDE drive. The DTK BIOS CMOS must be configured on first boot.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
from urllib.request import urlopen

from build_game import ROOT, DEFAULT_ADAPTER_FLAGS, build
from convert_large_v2 import convert, check_v2
from fat_image import FatImage, populate, scratch_hdd
from fetch_dependencies import CTMOUSE_EXE_SHA256
from fetch_roms import REV
from game_media import startup, embedded_boot
from iso_image import build_iso
from save_fixture import save_image

PROFILE = ROOT / 'profiles/1990-386sx25-family.cfg'
EMULATOR = ROOT / 'build/deps/86box-doscraft-386/86Box.exe'
EMULATOR_COMMIT = 'cc2ff8fe3058c4f8eec70cfac82b1909894d458f'
ROMS = ROOT / 'build/deps/86box/roms'
ROM_HASHES = {
    'machines/dtk386/3cto001.bin':
        'e47d098a00b7850b98ffe6ab698076a4a9eb7fae09f34ddd8120b8c7ddf9dd17',
    'video/tvga/tvga8900b.vbi':
        '00e17af370d3f243d068618a7e3e9f883ba0701b31de2446e46dd85f2664939f',
}
OUTPUT = ROOT / 'build/game/386sx25'
POINTER = OUTPUT / 'latest-play-vm.txt'
BIOS_SEED = OUTPUT / 'bios/dtk386.nvr'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check_bios(data):
    """Check the standard AT CMOS fields saved by this DTK BIOS setup."""
    if len(data) != 128 or data[0x10] != 0x42 or data[0x12] != 0xF0 or data[0x19] != 17 or \
            data[0x14] != 0x41 or struct.unpack_from('<H', data, 0x15)[0] != 640 or \
            struct.unpack_from('<H', data, 0x17)[0] != 3072 or \
            struct.unpack_from('<H', data, 0x30)[0] != 3072 or \
            sum(data[0x10:0x2E]) & 65535 != int.from_bytes(data[0x2E:0x30], 'big'):
        raise ValueError('DTK CMOS must match the 4 MiB, VGA, two-floppy, type-17 HDD profile')


def prepare(terrain_path, build_output=OUTPUT, smoke=False, bios=None, benchmark=False, update_pointer=True):
    if smoke and benchmark:
        raise ValueError('Choose smoke or benchmark media')
    if not EMULATOR.is_file():
        raise ValueError('Build the cc2ff8fe3 Trident-patched emulator into ' + str(EMULATOR.parent))
    for name, expected in ROM_HASHES.items():
        target = ROMS / name
        if not target.exists():
            data = urlopen(f'https://raw.githubusercontent.com/86Box/roms/{REV}/{name}', timeout=45).read()
            if digest(data) != expected:
                raise ValueError('Downloaded ROM checksum mismatch: ' + name)
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as stream:
                stream.write(data)
        if digest(target.read_bytes()) != expected:
            raise ValueError('Existing ROM checksum mismatch: ' + name)
    bios_data = None
    if bios:
        bios = Path(bios).resolve(strict=True)
        if bios.name != 'dtk386.nvr' or bios.stat().st_size != 128:
            raise ValueError('Use the same-board 128-byte dtk386.nvr CMOS')
        bios_data = bios.read_bytes()
        check_bios(bios_data)
    build_output = Path(build_output)
    manifest = json.loads((build_output / 'build.json').read_text())
    exe = (build_output / 'DOSCRAFT.EXE').read_bytes()
    flags = manifest.get('extra_adapter_flags', [])
    if manifest.get('status') != 'linked' or digest(exe) != manifest['exe_sha256'] or \
            not all(flag in flags for flag in ('-DDOS_LARGE_WORLD', '-DDOS_STREAM_LIGHT_CACHE', '-DDOS_FAR_FIELD')):
        raise ValueError('Expected a linked shared executable with large-world, light and distant-map support')
    if '-march=i386' not in manifest['link_command']:
        raise ValueError('Expected the shared 386-compatible executable')
    v1 = Path(terrain_path).read_bytes()
    terrain = convert(v1)
    check_v2(terrain, v1)
    deps = ROOT / 'build/deps'
    source = FatImage((deps / 'freedos/144m/x86BOOT.img').read_bytes())
    driver = (deps / 'ctmouse191/ctmouse.exe').read_bytes()
    if digest(driver) != CTMOUSE_EXE_SHA256:
        raise ValueError('Unexpected DOS mouse driver')
    files = [
        ('KERNEL.SYS', source.read('KERNEL.SYS')),
        ('COMMAND.COM', source.read('FREEDOS/BIN/COMMAND.COM')),
        ('CWSDPMI.EXE', (deps / 'cwsdpmi/bin/CWSDPMI.EXE').read_bytes()),
        ('CTMOUSE.EXE', driver), ('DOSCRAFT.EXE', exe),
        ('FDCONFIG.SYS', (ROOT / 'guest/FDCONFIG.SYS').read_text().replace('\n', '\r\n').encode('ascii')),
        ('AUTOEXEC.BAT', startup(smoke=smoke, benchmark=benchmark).replace(
            b'DOSCRAFT /ALL', b'DOSCRAFT /1') if benchmark == 'single' else startup(smoke=smoke, benchmark=benchmark)),
    ]
    boot = bytes(populate(source.data[:512], files))
    note = b'DOScraft: 386SX-25, 4 MiB, Trident VGA, 40 MB IDE.\r\nShared 386/486 executable; compact 1024-block seed 4242 world.\r\n'
    iso = build_iso(boot, {'DOSCRAFT.EXE': exe, 'README.TXT': note})
    if embedded_boot(iso) != boot:
        raise ValueError('ISO boot floppy roundtrip failed')
    disk = scratch_hdd(cylinders=977, heads=5, sectors=17)
    start = struct.unpack_from('<I', disk, 454)[0] * 512
    disk[start:] = populate(disk[start:start + 512], [('TERRAIN.TMP', terrain), ('WORLD.SAV', save_image())])
    volume = FatImage(disk, start)
    if volume.read('TERRAIN.TMP') != terrain or volume.read('WORLD.SAV') != save_image():
        raise ValueError('40 MB disk content roundtrip failed')
    runtime = ROOT / 'runtime'
    runtime.mkdir(exist_ok=True)
    prefix = 'bench-386-' if benchmark else 'game-smoke-386-' if smoke else 'game-386-'
    vm = Path(tempfile.mkdtemp(prefix=prefix, dir=runtime))
    (vm / 'boot.img').write_bytes(boot)
    (vm / 'DOSCRAFT.ISO').write_bytes(iso)
    (vm / 'hdd.img').write_bytes(disk)
    shutil.copy2(PROFILE, vm / '86box.cfg')
    if bios_data is not None:
        (vm / 'nvr').mkdir()
        (vm / 'nvr/dtk386.nvr').write_bytes(bios_data)
    record = {
        'stage': 'Private 386 VM; boot acceptance pending',
        'target': '386sx25-family-1990', 'vm_directory': str(vm),
        'smoke': smoke, 'benchmark': benchmark, 'large_world': True,
        'exe_sha256': digest(exe), 'boot_sha256': digest(boot), 'iso_sha256': digest(iso),
        'profile_sha256': digest(PROFILE.read_bytes()),
        'emulator_sha256': digest(EMULATOR.read_bytes()), 'emulator_commit': EMULATOR_COMMIT,
        'rom_revision': REV, 'rom_sha256': ROM_HASHES,
        'disk_bytes': len(disk), 'initial_hdd_sha256': digest(disk),
        'terrain_bytes': len(terrain), 'terrain_sha256': digest(terrain),
        'source_terrain': str(Path(terrain_path).resolve()), 'source_terrain_sha256': digest(v1),
        'build_output': str(build_output.resolve()),
        'bios_seed_sha256': digest(bios_data) if bios_data else None,
        'boot_note': 'Boot A: floppy; DTK BIOS HDD type 17 (977 cylinders, 5 heads, 17 sectors).',
        'audio': 'SILENT_DEFERRED',
    }
    (vm / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    if update_pointer:
        OUTPUT.mkdir(parents=True, exist_ok=True)
        pointer_name = 'latest-bench-vm.txt' if benchmark else 'latest-smoke-vm.txt' if smoke else POINTER.name
        (OUTPUT / pointer_name).write_text(str(vm))
    return vm, record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--terrain', type=Path, default=ROOT / 'build/checks/large-world-proto/terrain1024-crown-leaves.bin')
    parser.add_argument('--build-output', type=Path, default=OUTPUT)
    parser.add_argument('--build', action='store_true', help='Build the shared executable in the isolated 386 directory')
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--bios', type=Path)
    args = parser.parse_args()
    if args.bios is None and BIOS_SEED.is_file():
        args.bios = BIOS_SEED
    if args.build:
        build(output=args.build_output, extra_adapter_flags=(*DEFAULT_ADAPTER_FLAGS,
            '-DDOS_LARGE_WORLD', '-DDOS_STREAM_LIGHT_CACHE', '-DDOS_FAR_FIELD',
            '-DDOS_FAR_DETAILS', '-DDOS_FAR_VIEW_DEPTH', '-DDOS_FAR_REPROJECT'))
    vm, record = prepare(args.terrain, args.build_output, args.smoke, args.bios)
    print(f'VM={vm}\nTERRAIN_BYTES={record["terrain_bytes"]}\nHDD_BYTES={record["disk_bytes"]}')


if __name__ == '__main__':
    main()
