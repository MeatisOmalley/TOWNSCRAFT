"""The 1996 DOScraft VM: Pentium 200, Matrox Millennium, optional Voodoo Graphics.

A private VM directory holds:

  DOSCRA96.ISO        D96REF, D96SOFT, D963DFX, SOUND.PAK and the boot floppy
  boot.img            the 2.88-MiB floppy embedded in that ISO (the three
                      executables do not fit a 1.44-MiB disk)
  scratch-large.img   writable 206-MB SCSI disk: the published prebaked 1024
                      seed (TERRAIN.TMP) and a blank WORLD.SAV
  nvr/                the P/I-P55T2P4 CMOS and flash (ESCD) seed
  86box.cfg           profiles/1996-p200.cfg: Matrox Millennium only
  86box-3dfx.cfg      the same machine with the Voodoo Graphics enabled

Both configurations share the disk, CMOS and boot floppy: one machine, with
and without the 3D board. The boot floppy starts D963DFX and falls back to
D96SOFT when no Voodoo answers on PCI. Existing VMs are never edited; every
prepare call allocates a new directory under runtime/.
"""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

import build_1996
from build_game import ROOT
from build_imported import COMPILER
from fat_image import FatImage, populate, scratch_hdd
from fetch_dependencies import CTMOUSE_EXE_SHA256
from fetch_roms import PATHS
from game_media import large_volume
from iso_image import build_iso
from large_world_media import check_terrain
from save_fixture import save_image

PROFILE = ROOT / 'profiles/1996-p200.cfg'
EMULATOR = ROOT / 'build/deps/86box-doscraft-1996/86Box.exe'
ROMS = ROOT / 'build/deps/86box/roms'
OUTPUT = build_1996.OUTPUT
POINTER = OUTPUT / 'latest-1996-vm.txt'
TARGET = 'p5-200-1996'
ISO_NAME = 'DOSCRA96.ISO'
DISK = 'scratch-large.img'
DISK_BYTES = 800 * 8 * 63 * 512
ED_FLOPPY_BYTES = 2880 * 1024
CONFIGS = {False: '86box.cfg', True: '86box-3dfx.cfg'}
CMOS_FILES = ('p55t2p4.nvr',)
REQUIRED_ROMS = (
    'machines/p55t2p4/0207_j2.bin',
    'video/matrox/matrox2064wr2.BIN',
    'sound/creative/CT4540 PnP.BIN',
    'sound/creative/awe32.raw',
    'scsi/ncr53c8xx/NCR307.BIN',
)
# The hardware the launchers and the benchmark insist on (86Box may add or
# drop default-valued keys when it rewrites a config).
REQUIRED = {
    'Machine/machine': 'p55t2p4', 'Machine/cpu_family': 'pentium_p54c',
    'Machine/cpu_speed': '200000000', 'Machine/cpu_multi': '3',
    'Machine/cpu_use_dynarec': '1', 'Machine/fpu_type': 'internal',
    'Machine/mem_size': '131072',
    'Video/gfxcard': 'millennium', 'Matrox Millennium/memory': '4',
    'Sound/sndcard': 'sbawe64_gold',
    'Storage controllers/scsicard_1': 'ncr53c875', 'NCR 53c875 #1/bios': '1',
    'Hard disks/hdd_01_parameters': '63, 8, 800, 0, scsi',
    'Hard disks/hdd_01_scsi_location': '0:00',
    'Hard disks/hdd_01_speed': 'st12550w_1996',
    'Hard disks/hdd_01_fn': DISK,
    'Floppy and CD-ROM drives/fdd_01_type': '35_2ed',
    'Floppy and CD-ROM drives/fdd_01_fn': 'boot.img',
    'Input devices/mouse_type': 'msserial',
}
VOODOO = {'3Dfx Voodoo Graphics/type': '0',
          '3Dfx Voodoo Graphics/framebuffer_memory': '2',
          '3Dfx Voodoo Graphics/texture_memory': '2'}
VARIANT_TAG = {'ref': 'REF', 'soft': 'SOFT', '3dfx': '3DFX'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    return sha(Path(path).read_bytes())


def config_text(voodoo):
    text = PROFILE.read_text()
    if text.count('voodoo = 0\n') != 1:
        raise ValueError('Unexpected 1996 profile video section')
    return text.replace('voodoo = 0\n', 'voodoo = 1\n') if voodoo else text


def settings(path):
    result = {}
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(Path(path).read_text(encoding='utf-8-sig'))
    for section in parser.sections():
        for key, value in parser[section].items():
            result[f'{section}/{key}'] = value
    return result


def check_config(path, voodoo):
    actual = settings(path)
    wanted = {**REQUIRED, **(VOODOO if voodoo else {})}
    for key, value in wanted.items():
        if actual.get(key) != value:
            raise ValueError(f'Unexpected 1996 hardware in {Path(path).name}: {key}')
    if actual.get('Video/voodoo', '0') != ('1' if voodoo else '0'):
        raise ValueError(f'{Path(path).name} must {"enable" if voodoo else "disable"} the Voodoo')
    if actual.get('Machine/cpu_override_interpreter', '0') != '0':
        raise ValueError('CPU timing override refused')
    for key, value in actual.items():
        if key.startswith(('Serial Passthrough', 'Named Pipe (COM)')) or (
                key.startswith('Ports (COM & LPT)/serial') and key.endswith('_device') and value != 'none'):
            raise ValueError('External serial passthrough refused')


def ed_floppy(files):
    """A FreeDOS-bootable 2.88-MiB FAT12 floppy (80 cylinders, 2 heads, 36 sectors)."""
    source = (ROOT / 'build/deps/freedos/144m/x86BOOT.img').read_bytes()
    boot = bytearray(source[:512])
    struct.pack_into('<HBHBHHBH', boot, 11, 512, 2, 1, 2, 240, 5760, 0xf0, 9)
    struct.pack_into('<HHII', boot, 24, 36, 2, 0, 0)
    image = populate(bytes(boot), files)
    if len(image) != ED_FLOPPY_BYTES or FatImage(image).bits != 12:
        raise ValueError('2.88-MiB floppy layout mismatch')
    return image


def startup(mode, variant=None, benchmark=None):
    """AUTOEXEC.BAT for the player, the three-executable smoke or one benchmark."""
    lines = ['@ECHO OFF', 'SET PATH=A:\\', 'SET BLASTER=A220 I7 D1 H5 P330 T6',
             'CTMOUSE /S14 /R11 /W /Y', 'CWSDPMI -p -s-', 'AWECFG']
    if mode == 'play':
        lines += ['ECHO DOScraft 1996: D963DFX (Voodoo), D96SOFT (Pentium), D96REF (486 renderer).',
                  'D963DFX', 'IF ERRORLEVEL 1 D96SOFT',
                  'ECHO Type D963DFX, D96SOFT or D96REF to play again.']
    elif mode == 'smoke':
        for name in build_1996.VARIANTS:
            exe, tag = build_1996.EXE[name][:-4], VARIANT_TAG[name]
            lines += [f'{exe} /SMOKE', f'{exe} /SMOKELOAD',
                      f'IF EXIST C:\\PLAYNEW.TXT COPY C:\\PLAYNEW.TXT C:\\{tag}NEW.TXT',
                      f'IF EXIST C:\\PLAYLOAD.TXT COPY C:\\PLAYLOAD.TXT C:\\{tag}LOAD.TXT',
                      'IF EXIST C:\\PLAYNEW.TXT DEL C:\\PLAYNEW.TXT',
                      'IF EXIST C:\\PLAYLOAD.TXT DEL C:\\PLAYLOAD.TXT']
        lines += ['ECHO PASS> C:\\SMOKE.END', 'ECHO Smoke finished.']
    elif mode == 'bench':
        exe = build_1996.EXE[variant][:-4]
        if benchmark == 'sweep':
            lines += [f'{exe} /SWEEP']
        else:
            lines += [f'{exe} /1', 'IF ERRORLEVEL 1 GOTO DONE', f'{exe} /2',
                      'IF ERRORLEVEL 1 GOTO DONE', f'{exe} /3']
        lines += [':DONE', 'ECHO Benchmark finished.']
    elif mode == 'probe':
        lines += ['ECHO 1996 probe: DOS prompt only.']
    else:
        raise ValueError(f'Unknown startup mode {mode!r}')
    return ('\r\n'.join(lines) + '\r\n').encode('ascii')


def awecfg():
    """guest/awecfg.c: the AWE64 PnP resource setup run before the game."""
    source = ROOT / 'guest/awecfg.c'
    target = OUTPUT / 'AWECFG.EXE'
    stamp = OUTPUT / 'AWECFG.src.sha256'
    if not target.exists() or not stamp.exists() or stamp.read_text() != file_sha(source):
        OUTPUT.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(COMPILER), '-march=i386', '-O2', '-s', '-Wall', '-Wextra',
                        str(source), '-o', str(target)], check=True)
        stamp.write_text(file_sha(source))
    return target.read_bytes()


def read_build(variant, output=None):
    output = Path(output or OUTPUT / variant)
    record = json.loads((output / 'build.json').read_text())
    exe = (output / build_1996.EXE[variant]).read_bytes()
    if record.get('status') != 'linked' or sha(exe) != record['exe_sha256'] or \
       record.get('r96_variant') != variant:
        raise ValueError(f'Build a fresh linked {variant} executable first')
    return record, exe


def media(executables, bank, mode, variant=None, benchmark=None, music=None):
    """The ISO and its embedded 2.88-MiB boot floppy."""
    deps = ROOT / 'build/deps'
    driver = (deps / 'ctmouse191/ctmouse.exe').read_bytes()
    if sha(driver) != CTMOUSE_EXE_SHA256:
        raise ValueError('Unrecognized mouse driver')
    source = FatImage((deps / 'freedos/144m/x86BOOT.img').read_bytes())
    files = [('KERNEL.SYS', source.read('KERNEL.SYS')),
             ('COMMAND.COM', source.read('FREEDOS/BIN/COMMAND.COM')),
             ('CWSDPMI.EXE', (deps / 'cwsdpmi/bin/CWSDPMI.EXE').read_bytes()),
             ('CTMOUSE.EXE', driver), ('AWECFG.EXE', awecfg()),
             ('FDCONFIG.SYS', (ROOT / 'guest/FDCONFIG.SYS').read_text().replace('\n', '\r\n').encode('ascii')),
             ('AUTOEXEC.BAT', startup(mode, variant, benchmark))]
    files += sorted(executables.items())
    extras = {**({'SOUND.PAK': bank} if bank is not None else {}),
              **({'MUSIC.PAK': music} if music is not None else {})}
    files += sorted(extras.items())
    boot = ed_floppy(files)
    note = ('DOScraft 1996: Pentium 200, Matrox Millennium, optional 3dfx Voodoo Graphics.\r\n'
            'D963DFX.EXE  Voodoo Graphics renderer (own SST-1 driver), 640x480 16-bit.\r\n'
            'D96SOFT.EXE  Pentium software renderer, Mode X on any VGA.\r\n'
            'D96REF.EXE   the shipping 386/486 renderer, for comparison.\r\n'
            'The launchers boot the 2.88-MiB floppy embedded in this ISO.\r\n'
            'WASD walk, arrows look, J break, E place/use, Tab inventory, C craft.\r\n'
            'Space new world; L load; F9 save. Wait for save completion before closing.\r\n'
            'See HARDWARE_1996.md in the repository for sources and limitations.\r\n').encode('ascii')
    iso = build_iso(boot, {**executables, **extras, 'README.TXT': note}, volume_id='DOSCRAFT_1996')
    if embedded_boot(iso) != boot:
        raise ValueError('ISO embedded boot roundtrip failed')
    return iso, boot


def embedded_boot(image):
    """The 2.88-MiB floppy-emulation image of a 1996 ISO, strictly checked."""
    if image[16 * 2048:16 * 2048 + 7] != b'\1CD001\1' or image[17 * 2048:17 * 2048 + 7] != b'\0CD001\1':
        raise ValueError('Invalid ISO descriptors')
    catalog = struct.unpack_from('<I', image, 17 * 2048 + 71)[0] * 2048
    data = image[catalog:catalog + 2048]
    if (data[:4] != b'\1\0\0\0' or data[30:38] != b'\x55\xaa\x88\3\0\0\0\0' or
            sum(struct.unpack('<16H', data[:32])) & 65535):
        raise ValueError('Expected a 2.88-MiB El Torito floppy catalog')
    offset = struct.unpack_from('<I', data, 40)[0] * 2048
    boot = image[offset:offset + ED_FLOPPY_BYTES]
    if len(boot) != ED_FLOPPY_BYTES or boot[510:512] != b'\x55\xaa':
        raise ValueError('Invalid embedded floppy')
    return boot


def published_player():
    pointer = ROOT / 'build/game/486dx25/latest-play-vm.txt'
    player = Path(pointer.read_text().strip()).resolve(strict=True)
    return player, json.loads((player / 'build.json').read_text())


def published_music():
    """The release's MUSIC.PAK (songs built from user-supplied MIDI files that
    are not in the repository), from its boot floppy, checked by hash."""
    player, record = published_player()
    if not record.get('music_pack'):
        return None
    music = FatImage((player / 'boot.img').read_bytes()).read('MUSIC.PAK')
    if sha(music) != record['music_pack']['sha256']:
        raise ValueError('Published MUSIC.PAK differs from its release manifest')
    return music


def published_terrain():
    """The published prebaked 1024 seed, read from the stopped-or-running player
    disk without writing it. Its hash must equal the published manifest."""
    player, record = published_player()
    if not record.get('large_world') or record.get('smoke') or record.get('benchmark'):
        raise ValueError('The published player is not the large-world release')
    terrain = large_volume((player / DISK).read_bytes()).read('TERRAIN.TMP')
    if sha(terrain) != record['terrain_sha256']:
        raise ValueError('Published player terrain differs from its release manifest')
    check_terrain(terrain)
    return terrain, record


def large_disk(terrain):
    disk = scratch_hdd(cylinders=800, heads=8, sectors=63)
    start = struct.unpack_from('<I', disk, 454)[0] * 512
    disk[start:] = populate(disk[start:start + 512],
                            [('TERRAIN.TMP', terrain), ('WORLD.SAV', save_image())])
    if large_volume(disk).read('TERRAIN.TMP') != terrain:
        raise ValueError('New HDD terrain verification failed')
    return disk


def command(vm, voodoo):
    vm = Path(vm)
    return [str(EMULATOR), '-P', str(vm), '-C', CONFIGS[voodoo], '-R', str(ROMS)]


def check_runtime(record=None):
    if not EMULATOR.is_file():
        raise ValueError('Build the 1996 emulator first (tools/build_patched_86box.py)')
    for name in REQUIRED_ROMS:
        if file_sha(ROMS / name) != PATHS[name]:
            raise ValueError('Missing or changed ROM: ' + name)
    if record is not None:
        if file_sha(EMULATOR) != record['emulator_sha256']:
            raise ValueError('1996 emulator differs from the VM manifest')
        if file_sha(PROFILE) != record['profile_sha256']:
            raise ValueError('1996 hardware profile differs from the VM manifest')


def prepare(kind='play', cmos=None, builds=None, variant=None, benchmark=None, prefix=None,
            update_pointer=None):
    """Create a NEW private VM. kind: play, smoke, probe or bench.

    builds maps variant -> build directory (defaults to build/game/p5-200/<v>);
    a bench VM carries only the one benchmark executable it runs.
    """
    check_runtime()
    names = (variant,) if kind == 'bench' else build_1996.VARIANTS
    builds = builds or {}
    records, executables, bank = {}, {}, None
    for name in names:
        record, exe = read_build(name, builds.get(name))
        records[name] = record
        executables[build_1996.EXE[name]] = exe
        if record.get('sound_bank'):
            data = Path(record['sound_bank']['path']).read_bytes()
            if sha(data) != record['sound_bank']['sha256']:
                raise ValueError('Sound bank checksum mismatch')
            if bank is not None and data != bank:
                raise ValueError('The executables disagree on SOUND.PAK')
            bank = data
    music = published_music() if bank is not None else None
    iso, boot = media(executables, bank, kind, variant, benchmark, music)
    terrain, published = published_terrain()
    runtime = ROOT / 'runtime'
    runtime.mkdir(exist_ok=True)
    runtime = runtime.resolve()  # a worktree's runtime/ may be a junction
    prefix = prefix or {'play': 'game-1996-', 'smoke': 'game-smoke-1996-',
                        'probe': 'probe-1996-', 'bench': 'bench-1996-'}[kind]
    vm = Path(tempfile.mkdtemp(prefix=prefix, dir=runtime))
    with (vm / ISO_NAME).open('xb') as file:
        file.write(iso)
    with (vm / 'boot.img').open('xb') as file:
        file.write(boot)
    with (vm / DISK).open('xb') as file:
        file.write(large_disk(terrain))
    for voodoo, name in CONFIGS.items():
        (vm / name).write_text(config_text(voodoo), encoding='utf-8')
    cmos_record = None
    if cmos is not None:
        cmos = Path(cmos).resolve(strict=True)
        shutil.copytree(cmos, vm / 'nvr')
        cmos_record = {path.name: file_sha(path) for path in sorted((vm / 'nvr').iterdir())}
        if not all(name in cmos_record for name in CMOS_FILES):
            raise ValueError('CMOS seed lacks ' + ', '.join(CMOS_FILES))
    record = {
        'stage': {'play': '1996 hardware VM; interactive acceptance pending',
                  'smoke': '1996 three-executable smoke', 'probe': '1996 hardware probe',
                  'bench': '1996 benchmark'}[kind],
        'target': TARGET, 'hardware_era': '1996', 'vm_directory': str(vm),
        'smoke': kind == 'smoke', 'benchmark': benchmark if kind == 'bench' else False,
        'probe': kind == 'probe', 'variant': variant,
        'executables': {build_1996.EXE[name]: rec['exe_sha256'] for name, rec in records.items()},
        'build_dirs': {name: str(Path(builds.get(name) or OUTPUT / name)) for name in names},
        'iso_sha256': sha(iso), 'boot_sha256': sha(boot),
        'sound_bank_sha256': sha(bank) if bank is not None else None,
        'music_pack_sha256': sha(music) if music is not None else None,
        'profile_sha256': file_sha(PROFILE), 'emulator_sha256': file_sha(EMULATOR),
        'rom_sha256': {name: PATHS[name] for name in REQUIRED_ROMS},
        'cmos_sha256': cmos_record,
        'large_world': True, 'terrain_sha256': sha(terrain),
        'terrain_header_sha256': sha(terrain[:97 * 512]),
        'published_player_vm': published['vm_directory'],
        'disk_note': 'Private 206 MB FAT16 SCSI disk with the published prebaked 1024 seed; '
                     'Seagate Barracuda 2 ST12550W seek/rotation/cache timing proxy, not its 2.1 GB capacity',
        'scsi_note': 'Symbios/NCR 53C875 Ultra Wide PCI with SDMS 3.07 BIOS; 86Box has no Adaptec AIC-7880',
        'boot_note': '2.88-MiB floppy extracted from the ISO; no native CD boot claim',
    }
    (vm / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    if update_pointer if update_pointer is not None else kind == 'play':
        POINTER.write_text(str(vm))
    return vm, record


def validate(vm, voodoo):
    """Read-only check of a prepared VM; returns its 86Box command line."""
    vm = Path(vm).resolve(strict=True)
    record = json.loads((vm / 'build.json').read_text())
    if Path(record['vm_directory']).resolve() != vm or record.get('target') != TARGET:
        raise ValueError('Not a 1996 DOScraft VM')
    check_runtime(record)
    check_config(vm / CONFIGS[voodoo], voodoo)
    iso = (vm / ISO_NAME).read_bytes()
    boot = (vm / 'boot.img').read_bytes()
    if sha(iso) != record['iso_sha256'] or sha(boot) != record['boot_sha256'] or embedded_boot(iso) != boot:
        raise ValueError('ISO/boot media differs from the prepared VM')
    floppy = FatImage(boot)
    for name, digest in record['executables'].items():
        if sha(floppy.read(name)) != digest:
            raise ValueError('Boot floppy executable differs: ' + name)
    if (vm / DISK).stat().st_size != DISK_BYTES:
        raise ValueError('1996 writable HDD is missing or has unexpected geometry')
    if record.get('cmos_sha256') is not None:
        for name in CMOS_FILES:
            if not (vm / 'nvr' / name).is_file():
                raise ValueError('1996 CMOS is missing: ' + name)
    return command(vm, voodoo)


def running(vm):
    query = ("$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath; "
             "Get-CimInstance Win32_Process -Filter \"Name = '86Box.exe'\" | "
             "Select-Object ProcessId,ExecutablePath,CommandLine | ConvertTo-Json -Compress")
    result = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-Command', query],
                            check=True, capture_output=True, text=True)
    rows = json.loads(result.stdout) if result.stdout.strip() else []
    rows = [rows] if isinstance(rows, dict) else rows
    return [row for row in rows
            if str(row.get('ExecutablePath', '')).casefold() == str(EMULATOR).casefold()
            and str(vm).casefold() in str(row.get('CommandLine', '')).casefold()]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('kind', choices=('play', 'smoke', 'probe'))
    parser.add_argument('--cmos', type=Path, help='nvr directory of a configured P/I-P55T2P4')
    parser.add_argument('--launch', action='store_true')
    parser.add_argument('--voodoo', action='store_true', help='Launch with the Voodoo enabled')
    args = parser.parse_args()
    if args.kind != 'probe' and args.cmos is None:
        parser.error('--cmos is required except for a probe')
    vm, _ = prepare(args.kind, args.cmos)
    cmd = validate(vm, args.voodoo)
    print(f'VM={vm}')
    if args.launch:
        process = subprocess.Popen(cmd, cwd=vm)
        print(f'PID={process.pid}')


if __name__ == '__main__':
    main()
