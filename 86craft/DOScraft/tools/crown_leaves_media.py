"""Clone a stopped player VM with face-adjacent distant crowns; retain its save.

Only baked leaf eligibility and affected summary/checksum bytes change. The
source disk is never written. --install-pointer selects the checked clone.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from build_game import ROOT
from fat_image import FatImage,populate
from leaf_flags_media import upgrade
from publish_game import atomic_write

CROWN_TERRAIN=ROOT/'build/checks/large-world-proto/terrain1024-crown-leaves.bin'


def digest(data):return hashlib.sha256(data).hexdigest()


def running(vm):
    query=('Get-CimInstance Win32_Process -Filter "Name = \'86Box.exe\'" | '
           'Select-Object CommandLine | ConvertTo-Json -Compress')
    rows=json.loads(subprocess.check_output(['powershell.exe','-NoProfile','-Command',query],text=True) or '[]')
    if isinstance(rows,dict):rows=[rows]
    return any(str(vm).casefold() in str(row.get('CommandLine','')).casefold() for row in rows)


def migrate_disk(disk,terrain):
    part=struct.unpack_from('<I',disk,454)[0]*512
    old=FatImage(disk,part)
    if any(attr&16 for name,attr,cluster,size in old.directory()):
        raise ValueError('Unexpected directory on player disk')
    merged,save=upgrade(old.read('TERRAIN.TMP'),terrain,old.read('WORLD.SAV'))
    files=[(name,merged if name=='TERRAIN.TMP' else save if name=='WORLD.SAV' else old.read(name))
           for name,attr,cluster,size in old.directory()]
    result=bytearray(disk);result[part:]=populate(disk[part:part+512],files)
    if len(result)!=len(disk):raise ValueError('Player disk geometry changed')
    check=FatImage(result,part)
    for name,data in files:
        if check.read(name)!=data:raise ValueError('Migrated disk roundtrip failed: '+name)
    return bytes(result),merged,save


def stage(vm,terrain,install_pointer=None):
    vm=Path(vm).resolve(strict=True)
    if not vm.is_relative_to(ROOT/'runtime') or running(vm):
        raise ValueError('Save and close this private player VM before migrating')
    record=json.loads((vm/'build.json').read_text())
    if record['vm_directory']!=str(vm) or record.get('smoke') or record.get('benchmark'):
        raise ValueError('Expected a player VM')
    paths=[vm/name for name in ['hdd.img','scratch-large.img'] if (vm/name).is_file()]
    if len(paths)!=1:raise ValueError('Expected one large-world player disk')
    disk_path=paths[0];original=disk_path.read_bytes()
    source=Path(terrain).resolve(strict=True);data=source.read_bytes()
    fs=FatImage(original,struct.unpack_from('<I',original,454)[0]*512)
    if fs.read('TERRAIN.TMP')[:16]==b'DOSCRAFT-1024-V2':
        from convert_large_v2 import convert
        pairs=struct.unpack_from('<I',fs.read('TERRAIN.TMP'),136)[0]
        data=convert(data,pool_pairs=pairs)
    disk,merged,save=migrate_disk(original,data)
    if running(vm) or disk_path.read_bytes()!=original:
        raise ValueError('Player changed during migration; no pointer changed')
    result=Path(tempfile.mkdtemp(prefix='game-crown-',dir=ROOT/'runtime'))
    shutil.copytree(vm,result,dirs_exist_ok=True)
    if (result/disk_path.name).read_bytes()!=original:
        raise ValueError('Source disk changed during copy')
    (result/disk_path.name).write_bytes(disk)
    proof={'source_vm':str(vm),'source_disk_sha256':digest(original),'disk_sha256':digest(disk),
           'save_before_sha256':digest(fs.read('WORLD.SAV')),'save_after_sha256':digest(save),
           'logical_blocks_and_player_state':'validated unchanged; leaf eligibility metadata only',
           'rule':'existing leaves sharing a face with the highest log in a stack'}
    updated={**record,'vm_directory':str(result),'initial_hdd_sha256':digest(disk),
             'terrain_bytes':len(merged),'terrain_sha256':digest(merged),
             'source_terrain':str(source),'source_terrain_sha256':digest(source.read_bytes()),
             'leaf_crown_upgrade':proof}
    if 'terrain_header_sha256' in record:
        updated['terrain_header_sha256']=digest(merged[:(97 if merged[:16]==b'DOSCRAFT-1024-V2' else 33)*512])
    (result/'build.json').write_text(json.dumps(updated,indent=2)+'\n')
    if install_pointer:
        pointer=Path(install_pointer).resolve(strict=True)
        if Path(pointer.read_text().strip()).resolve()!=vm or running(vm) or disk_path.read_bytes()!=original:
            raise ValueError('Player changed; migrated clone is staged but pointer unchanged')
        atomic_write(pointer,str(result).encode('utf-8'))
    return result,proof


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vm',type=Path,required=True)
    p.add_argument('--terrain',type=Path,default=CROWN_TERRAIN)
    p.add_argument('--install-pointer',type=Path)
    args=p.parse_args();vm,proof=stage(args.vm,args.terrain,args.install_pointer)
    print(json.dumps({'vm':str(vm),**proof},indent=2))


if __name__=='__main__':main()
