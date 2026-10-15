"""Publish a freshly built playable ISO without losing the previous HDD/ISO.

Refuses live VMs. A new VM receives an exact copy of the old player's HDD;
the old VM/disk remain untouched. Only validated media is published, with an
old-ISO backup and atomic replacement/pointer writes. No benchmark executable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from build_game import ROOT,OUTPUT
from game_media import prepare,validate
from prepare_large_vm import attach_large_disk
from large_world_media import upgrade_disk


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def running(vm):
    result=subprocess.run(['powershell.exe','-NoLogo','-NoProfile','-Command',
        "$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath; Get-CimInstance Win32_Process -Filter \"Name = '86Box.exe'\" | Select-Object ExecutablePath,CommandLine | ConvertTo-Json -Compress"],
        check=True,capture_output=True,text=True)
    processes=json.loads(result.stdout) if result.stdout.strip() else []
    if isinstance(processes,dict): processes=[processes]
    emulators={str(ROOT/'build/deps/86box/86Box.exe').casefold(),
               str(ROOT/'build/deps/86box-doscraft/86Box.exe').casefold(),
               str(ROOT/'build/deps/86box-doscraft-40/86Box.exe').casefold(),
               str(ROOT/'build/deps/86box-doscraft-1990/86Box.exe').casefold()}
    return any(str(p.get('ExecutablePath','')).casefold() in emulators and
               str(vm).casefold() in str(p.get('CommandLine','')).casefold() for p in processes)


def atomic_write(path,data):
    handle,name=tempfile.mkstemp(prefix='.publish-',dir=path.parent)
    temporary=Path(name)
    try:
        with os.fdopen(handle,'wb') as f: f.write(data)
        os.replace(temporary,path)
    finally:
        if temporary.exists(): temporary.unlink() # This exact new private file only.


def publish(bios):
    pointer=OUTPUT/'latest-play-vm.txt'
    old=Path(pointer.read_text().strip()).resolve(strict=True)
    if not old.is_relative_to(ROOT/'runtime'): raise ValueError('Expected private player VM')
    previous=json.loads((old/'build.json').read_text())
    build=json.loads((OUTPUT/'build.json').read_text())
    if previous.get('smoke') or previous.get('benchmark') or build.get('benchmark'):
        raise ValueError('Cannot publish diagnostic media as the playable game')
    if build.get('smoke_verified_exe_sha256')!=build.get('exe_sha256') or not build.get('exe_sha256'):
        raise ValueError('Run game_smoke.py successfully on this exact build before publishing')
    iso=ROOT/'ISO/DOSCRAFT.ISO'
    if sha(iso)!=previous['iso_sha256']: raise ValueError('Player ISO differs from its manifest')
    if running(old): raise ValueError('Close the player VM before publishing; its disk is live')
    prepare_options={'bios':bios,'update_pointer':False}
    if previous.get('hardware_era')=='1990-06-30':
        prepare_options['hardware_vm']=old
    vm,record=prepare(**prepare_options)
    disk_hash=sha(old/'scratch.img')
    shutil.copyfile(old/'scratch.img',vm/'scratch.img')
    if sha(vm/'scratch.img')!=disk_hash or sha(old/'scratch.img')!=disk_hash or running(old):
        raise ValueError('Player HDD changed during copy; publishing refused')
    validate(vm)
    if Path(pointer.read_text().strip()).resolve()!=old or sha(iso)!=previous['iso_sha256']:
        raise ValueError('Player launcher/media changed concurrently; publishing refused')
    backup=iso.with_name('DOSCRAFT.before-'+previous['exe_sha256'][:12]+'.ISO')
    if backup.exists():
        if sha(backup)!=previous['iso_sha256']: raise ValueError('Unknown backup already exists')
    else:
        with backup.open('xb') as f: f.write(iso.read_bytes())
    record['preserved_player_vm']=str(old)
    record['preserved_hdd_sha256']=disk_hash
    (vm/'build.json').write_text(json.dumps(record,indent=2)+'\n')
    atomic_write(iso,(vm/'DOSCRAFT.ISO').read_bytes())
    try: atomic_write(pointer,str(vm).encode('utf-8'))
    except Exception:
        atomic_write(iso,backup.read_bytes()); raise
    print(f'ISO={iso}\nVM={vm}\nBACKUP={backup}\nPRESERVED_HDD={disk_hash}')
    return vm


def publish_large(terrain_path, smoke_vm, output):
    """Publish an exact smoke-tested 1024 build with its own writable HDD."""
    output=Path(output).resolve(strict=True)
    terrain_path=Path(terrain_path).resolve(strict=True)
    smoke_vm=Path(smoke_vm).resolve(strict=True)
    pointer=OUTPUT/'latest-play-vm.txt'
    old=Path(pointer.read_text().strip()).resolve(strict=True)
    if not old.is_relative_to(ROOT/'runtime') or not smoke_vm.is_relative_to(ROOT/'runtime'):
        raise ValueError('Expected private DOScraft runtime VMs')
    previous=json.loads((old/'build.json').read_text())
    tested=json.loads((smoke_vm/'build.json').read_text())
    build=json.loads((output/'build.json').read_text())
    report=json.loads((smoke_vm/'large-world-smoke.json').read_text())
    if (previous.get('smoke') or previous.get('benchmark') or
        not tested.get('smoke') or not tested.get('large_world') or
        '-DDOS_LARGE_WORLD' not in build.get('extra_adapter_flags',()) or
        build.get('benchmark') or tested.get('exe_sha256')!=build.get('exe_sha256') or
        tested.get('source_terrain_sha256',tested.get('terrain_sha256'))!=sha(terrain_path) or
        report.get('save_version')!=5 or
        any('RESULT=PASS\n' not in report.get(mode,'') for mode in ('new','load'))):
        raise ValueError('Large-world media is not the exact smoke-tested build')
    iso=ROOT/'ISO/DOSCRAFT.ISO'
    if sha(iso)!=previous['iso_sha256'] or running(old):
        raise ValueError('Current player media changed or VM is running')
    vm,record=prepare(smoke=False,output=output,hardware_vm=old,update_pointer=False)
    attach_large_disk(vm,terrain_path,player=True)
    record=json.loads((vm/'build.json').read_text())
    disk_name='scratch-large.img' if previous.get('large_world') else 'scratch.img'
    old_disk=old/disk_name
    old_disk_hash=sha(old_disk)
    # A transport/code update on the same baked seed must retain world edits
    # and saves. The old private VM remains the recovery copy.
    if previous.get('large_world') and previous.get('terrain_format')=='V2 RLE' and previous.get('terrain_sha256')==sha(terrain_path):
        shutil.copyfile(old_disk,vm/'scratch-large.img')
        if sha(vm/'scratch-large.img')!=old_disk_hash:
            raise ValueError('Player large-world disk changed during copy')
        record['preserved_hdd_sha256']=old_disk_hash
    elif previous.get('large_world'):
        # A lighting appendix changes the fixture hash but retains the seed,
        # base column index and every edit/save offset. Fail closed on a seed
        # mismatch instead of silently replacing the player's world.
        (vm/'scratch-large.img').write_bytes(upgrade_disk(old_disk.read_bytes(),terrain_path.read_bytes(),compact=True))
        record['preserved_world_edits']=True
    validate(vm)
    if (running(old) or sha(old_disk)!=old_disk_hash or
        Path(pointer.read_text().strip()).resolve()!=old or
        sha(iso)!=previous['iso_sha256']):
        raise ValueError('Player VM or media changed during preparation')
    backup=iso.with_name('DOSCRAFT.before-'+previous['exe_sha256'][:12]+'.ISO')
    if backup.exists():
        if sha(backup)!=previous['iso_sha256']:
            raise ValueError('Unknown ISO backup already exists')
    else:
        with backup.open('xb') as f: f.write(iso.read_bytes())
    record['preserved_player_vm']=str(old)
    record['preserved_hdd_sha256']=old_disk_hash
    record['verified_smoke_vm']=str(smoke_vm)
    (vm/'build.json').write_text(json.dumps(record,indent=2)+'\n')
    atomic_write(iso,(vm/'DOSCRAFT.ISO').read_bytes())
    try: atomic_write(pointer,str(vm).encode('utf-8'))
    except Exception:
        atomic_write(iso,backup.read_bytes())
        raise
    print(f'ISO={iso}\nVM={vm}\nBACKUP={backup}\nPRESERVED_VM={old}')
    return vm


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bios',type=Path)
    parser.add_argument('--large-terrain',type=Path)
    parser.add_argument('--large-smoke-vm',type=Path)
    parser.add_argument('--build-output',type=Path,default=OUTPUT)
    args=parser.parse_args()
    if args.large_terrain or args.large_smoke_vm:
        if not args.large_terrain or not args.large_smoke_vm or args.bios:
            parser.error('Large publication requires --large-terrain and --large-smoke-vm only')
        publish_large(args.large_terrain,args.large_smoke_vm,args.build_output)
    else:
        if not args.bios: parser.error('Legacy publication requires --bios')
        publish(args.bios)


if __name__=='__main__': main()
