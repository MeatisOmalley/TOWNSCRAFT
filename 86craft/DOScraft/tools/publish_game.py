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


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def running(vm):
    result=subprocess.run(['powershell.exe','-NoLogo','-NoProfile','-Command',
        "$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath; Get-CimInstance Win32_Process -Filter \"Name = '86Box.exe'\" | Select-Object ExecutablePath,CommandLine | ConvertTo-Json -Compress"],
        check=True,capture_output=True,text=True)
    processes=json.loads(result.stdout) if result.stdout.strip() else []
    if isinstance(processes,dict): processes=[processes]
    emulators={str(ROOT/'build/deps/86box/86Box.exe').casefold(),
               str(ROOT/'build/deps/86box-doscraft/86Box.exe').casefold()}
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
    vm,record=prepare(bios=bios,update_pointer=False)
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


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bios',required=True,type=Path)
    args=parser.parse_args(); publish(args.bios)


if __name__=='__main__': main()
