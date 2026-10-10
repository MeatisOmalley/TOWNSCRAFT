"""Package the real game and prepare a NEW private VM; never replace a save disk.

The ISA-486 BIOS boots A:, not El Torito. Our launcher uses the exact floppy
embedded in the ISO. No CD controller/driver or firmware capability is invented.
"""
import argparse
import configparser
import hashlib
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from fat_image import FatImage, populate, scratch_hdd
from iso_image import build_iso
from build_game import ROOT, OUTPUT
from fetch_dependencies import CTMOUSE_EXE_SHA256
from run_vm import launch_command, diagnostic_volume
from save_fixture import save_image
from terrain_fixture import terrain_image


def sha(data):
    return hashlib.sha256(data).hexdigest()


def startup(smoke=False):
    lines=['@ECHO OFF','SET PATH=A:\\','CTMOUSE /S14 /R11 /W /Y',
           'CWSDPMI -p -s-']
    if smoke:
        lines += ['DOSCRAFT /SMOKE','IF ERRORLEVEL 1 GOTO DONE',
                  'DOSCRAFT /SMOKELOAD',':DONE','ECHO Game test finished.']
    else:
        lines += ['DOSCRAFT','ECHO DOScraft exited. Audio is deferred.']
    return ('\r\n'.join(lines)+'\r\n').encode('ascii')


def embedded_boot(image):
    """Read and strictly check the generated x86 floppy-emulation catalog."""
    if len(image)%2048 or image[17*2048:17*2048+7]!=b'\0CD001\1':
        raise ValueError('Invalid ISO boot descriptor')
    if image[16*2048:16*2048+7]!=b'\1CD001\1':
        raise ValueError('Invalid ISO primary descriptor')
    if image[18*2048:18*2048+7]!=b'\xffCD001\1':
        raise ValueError('Invalid ISO descriptor terminator')
    at=17*2048
    if image[at+7:at+39]!=b'EL TORITO SPECIFICATION'.ljust(32,b'\0'):
        raise ValueError('Not an El Torito boot descriptor')
    catalog=struct.unpack_from('<I',image,at+71)[0]*2048
    data=image[catalog:catalog+2048]
    if (len(data)!=2048 or data[:4]!=b'\1\0\0\0' or
        data[30:38]!=b'\x55\xaa\x88\2\0\0\0\0' or
        sum(struct.unpack('<16H',data[:32]))&65535 or
        struct.unpack_from('<H',data,38)[0]!=1):
        raise ValueError('Invalid x86 floppy boot catalog')
    offset=struct.unpack_from('<I',data,40)[0]*2048
    boot=image[offset:offset+1440*1024]
    if len(boot)!=1440*1024 or boot[510:512]!=b'\x55\xaa':
        raise ValueError('Invalid embedded floppy')
    return boot


def prepare(smoke=False, bios=None):
    if bios:
        bios=Path(bios).resolve(strict=True)
        if bios.name!='isa486.nvr' or bios.stat().st_size!=128:
            raise ValueError('Expected known-good same-machine 128-byte isa486.nvr')
    build=json.loads((OUTPUT/'build.json').read_text())
    exe=(OUTPUT/'DOSCRAFT.EXE').read_bytes()
    if build.get('status')!='linked' or sha(exe)!=build['exe_sha256']:
        raise ValueError('Build a fresh successfully linked game first')
    deps=ROOT/'build/deps'
    driver=(deps/'ctmouse191/ctmouse.exe').read_bytes()
    if sha(driver)!=CTMOUSE_EXE_SHA256:
        raise ValueError('Unrecognized mouse driver')
    source=FatImage((deps/'freedos/144m/x86BOOT.img').read_bytes())
    files=[('KERNEL.SYS',source.read('KERNEL.SYS')),
           ('COMMAND.COM',source.read('FREEDOS/BIN/COMMAND.COM')),
           ('CWSDPMI.EXE',(deps/'cwsdpmi/bin/CWSDPMI.EXE').read_bytes()),
           ('CTMOUSE.EXE',driver),('DOSCRAFT.EXE',exe),
           ('FDCONFIG.SYS',(ROOT/'guest/FDCONFIG.SYS').read_text().replace('\n','\r\n').encode('ascii')),
           ('AUTOEXEC.BAT',startup(smoke))]
    boot=populate(source.data[:512],files)
    note=('DOScraft direct port: 486DX/25, 16 MiB, ET4000, 320x240 Mode X.\r\n'
          'Silent audio checkpoint; no renderer optimization.\r\n'
          'ISA486 launcher boots the floppy embedded in this ISO.\r\n'
          'Use a prepared writable HDD for TERRAIN.TMP and WORLD.SAV.\r\n'
          'WASD walk, arrows look, J break, E place/use, Tab inventory, C craft.\r\n'
          'Space new world; L load; F9 save. Wait for save completion before closing.\r\n'
          'See repository README for source, dependencies and limitations.\r\n').encode('ascii')
    iso=build_iso(boot,{'DOSCRAFT.EXE':exe,'README.TXT':note})
    if embedded_boot(iso)!=boot:
        raise ValueError('ISO embedded boot roundtrip failed')
    runtime=ROOT/'runtime'; runtime.mkdir(exist_ok=True)
    vm=Path(tempfile.mkdtemp(prefix='game-smoke-' if smoke else 'game-play-',dir=runtime))
    with (vm/'DOSCRAFT.ISO').open('xb') as f: f.write(iso)
    with (vm/'boot.img').open('xb') as f: f.write(embedded_boot(iso))
    disk=scratch_hdd(); start=struct.unpack_from('<I',disk,454)[0]*512
    disk[start:]=populate(disk[start:start+512],[('TERRAIN.TMP',terrain_image()),('WORLD.SAV',save_image())])
    with (vm/'scratch.img').open('xb') as f: f.write(disk)
    profile=ROOT/'profiles/486dx25-platform.cfg'
    config=profile.read_text()+('\n[Input devices]\nmouse_type = msserial\n'
        '\n[Microsoft Serial Mouse]\nport = 0\nbuttons = 2\n'
        '\n[Ports (COM & LPT)]\nserial1_enabled = 1\n')
    (vm/'86box.cfg').write_text(config,encoding='utf-8')
    if bios:
        (vm/'nvr').mkdir(); shutil.copyfile(bios,vm/'nvr/isa486.nvr')
    command=launch_command(vm) # Pinned executable and ROM checks before publishing.
    record={'stage':'Real game; silent audio; interactive acceptance pending',
        'smoke':smoke,'target':'486dx25','vm_directory':str(vm),
        'source_commit':build['source_commit'],'exe_sha256':sha(exe),
        'iso_sha256':sha(iso),'boot_sha256':sha(boot),'profile_sha256':sha(profile.read_bytes()),
        'bios_seed_sha256':sha(bios.read_bytes()) if bios else None,
        'emulator_sha256':sha(Path(command[0]).read_bytes()),
        'rom_sha256':{str(p.relative_to(deps/'86box/roms')):sha(p.read_bytes()) for p in
            (deps/'86box/roms'/s for s in ('machines/isa486/ISA-486.BIN',
                'video/et4000/ET4000_V8_06.BIN','hdd/esdi_at/62-000279-061.bin'))},
        'disk_note':'New 20 MiB FAT16 development HDD; 1989_3500rpm timing proxy, not final Wren V',
        'boot_note':'Floppy extracted from ISO; no native CD boot claim',
        'audio':'SILENT_DEFERRED'}
    (vm/'build.json').write_text(json.dumps(record,indent=2)+'\n')
    (OUTPUT/('latest-smoke-vm.txt' if smoke else 'latest-play-vm.txt')).write_text(str(vm))
    return vm,record


def validate(vm):
    vm=Path(vm).resolve(strict=True)
    record=json.loads((vm/'build.json').read_text())
    if Path(record['vm_directory']).resolve()!=vm or record['target']!='486dx25':
        raise ValueError('Incorrect game VM identity')
    settings=configparser.ConfigParser()
    settings.read(vm/'86box.cfg',encoding='utf-8-sig')
    required=configparser.ConfigParser()
    required.read(ROOT/'profiles/486dx25-platform.cfg')
    for section in ('Machine','Video','Tseng Labs ET4000AX (ISA)',
                    'Sound','Storage controllers','Hard disks','Floppy and CD-ROM drives'):
        for key,value in required.items(section):
            # 86Box legitimately omits these default values on config rewrite.
            defaults={'cpu_override_interpreter':'0','cpu_multi':'1',
                      'fpu_softfloat':'1','time_sync':'disabled',
                      'fdd_02_type':'none','hdd_01_esdi_channel':'0'}
            if settings.get(section,key,fallback=defaults.get(key))!=value:
                raise ValueError('Unexpected game hardware: '+section+'/'+key)
    for section,key,value,default in (('Input devices','mouse_type','msserial',None),
        ('Microsoft Serial Mouse','port','0','0'),('Microsoft Serial Mouse','buttons','2','2'),
        ('Ports (COM & LPT)','serial1_enabled','1','1')):
        if settings.get(section,key,fallback=default)!=value:
            raise ValueError('Unexpected game input hardware: '+section+'/'+key)
    for section in settings.sections():
        if section.startswith(('Serial Passthrough','Named Pipe (COM)','Virtual Console (COM)')):
            raise ValueError('External serial passthrough refused')
    if settings.has_section('Ports (COM & LPT)'):
        for key,value in settings.items('Ports (COM & LPT)'):
            if ((re.fullmatch(r'serial\d+_device',key) and value!='none') or
                (re.fullmatch(r'serial\d+_passthrough_enabled',key) and value!='0')):
                raise ValueError('External serial passthrough refused')
    iso=(vm/'DOSCRAFT.ISO').read_bytes(); boot=(vm/'boot.img').read_bytes()
    if sha(iso)!=record['iso_sha256'] or sha(boot)!=record['boot_sha256'] or embedded_boot(iso)!=boot:
        raise ValueError('ISO/boot media differs from prepared build')
    media=FatImage(boot)
    if sha(media.read('DOSCRAFT.EXE'))!=record['exe_sha256'] or media.read('AUTOEXEC.BAT')!=startup(record['smoke']):
        raise ValueError('Game payload or startup mismatch')
    if sha(media.read('CTMOUSE.EXE'))!=CTMOUSE_EXE_SHA256:
        raise ValueError('Mouse driver mismatch')
    if media.read('FDCONFIG.SYS')!=(ROOT/'guest/FDCONFIG.SYS').read_text().replace('\n','\r\n').encode('ascii'):
        raise ValueError('DOS startup configuration mismatch')
    volume=diagnostic_volume((vm/'scratch.img').read_bytes())
    terrain=volume.read('TERRAIN.TMP'); save=volume.read('WORLD.SAV')
    expected=terrain_image()
    if len(terrain)!=len(expected) or terrain[:512]!=expected[:512] or len(save)!=len(save_image()):
        raise ValueError('Unknown HDD terrain/save medium')
    # Never require a blank save for relaunch; player worlds belong to the user.
    return launch_command(vm)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--smoke',action='store_true')
    parser.add_argument('--bios',type=Path,help='Explicit known-good ISA486 CMOS seed')
    parser.add_argument('--launch',action='store_true')
    args=parser.parse_args()
    vm,_=prepare(args.smoke,args.bios)
    command=validate(vm)
    print(f'VM={vm}\nISO={vm / "DOSCRAFT.ISO"}')
    if args.launch:
        process=subprocess.Popen(command,cwd=vm)
        print(f'PID={process.pid}')


if __name__=='__main__': main()
