"""Clone stopped active player media into shared V2 RLE storage; keep backups."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import tempfile
from build_game import ROOT
from crown_leaves_media import running,digest
from compact_world_media import convert_player,grow_edit_pool,DEFAULT_POOL_PAIRS
from fat_image import FatImage
from large_world_media import replace_world_files
from publish_game import atomic_write


def install(pointer,boot_from=None,pool_pairs=DEFAULT_POOL_PAIRS):
    pointer=Path(pointer).resolve(strict=True)
    vm=Path(pointer.read_text().strip()).resolve(strict=True)
    if not vm.is_relative_to(ROOT/'runtime') or running(vm):
        raise ValueError('Save and close the player VM before converting')
    record=json.loads((vm/'build.json').read_text())
    if record.get('smoke') or record.get('benchmark') or record['vm_directory']!=str(vm):
        raise ValueError('Expected a player VM')
    boot_source=Path(boot_from).resolve(strict=True) if boot_from else vm
    boot_record=json.loads((boot_source/'build.json').read_text())
    boot=(boot_source/'boot.img').read_bytes()
    exe=FatImage(boot).read('DOSCRAFT.EXE')
    if (digest(boot)!=boot_record['boot_sha256'] or digest(exe)!=boot_record['exe_sha256'] or
            b'DOSCRAFT-1024-V2' not in exe):
        raise ValueError('Install the tested V2-capable shared release first, or supply --boot-from')
    files=[vm/n for n in ('hdd.img','scratch-large.img') if (vm/n).is_file()]
    if len(files)!=1:raise ValueError('Expected one large-world disk')
    source=files[0];original=source.read_bytes();part=struct.unpack_from('<I',original,454)[0]*512
    fs=FatImage(original,part);terrain=fs.read('TERRAIN.TMP')
    if terrain[:16]==b'DOSCRAFT-1024-V2':
        pairs=struct.unpack_from('<I',terrain,136)[0]
        if pairs>=pool_pairs:
            return {'vm':str(vm),'status':'already V2 RLE','terrain_bytes':len(terrain),'pool_pairs':pairs}
        compact=grow_edit_pool(terrain,pool_pairs);save=fs.read('WORLD.SAV')
        proof=dict(previous_pool_pairs=pairs,pool_pairs=pool_pairs,
                   previous_bytes=len(terrain),compact_bytes=len(compact),
                   existing_records='byte-identical; empty edit pairs appended')
        status='expanded V2 edit pool'
    else:
        compact,save,proof=convert_player(terrain,fs.read('WORLD.SAV'),pool_pairs=pool_pairs)
        status='converted to V2 RLE'
    disk=replace_world_files(original,compact,save)
    if running(vm) or source.read_bytes()!=original:
        raise ValueError('Player changed during conversion; pointer unchanged')
    result=Path(tempfile.mkdtemp(prefix='game-rle-',dir=ROOT/'runtime'))
    shutil.copytree(vm,result,dirs_exist_ok=True)
    if (result/source.name).read_bytes()!=original:raise ValueError('Source changed during copy')
    (result/source.name).write_bytes(disk)
    if boot_from:
        iso=(boot_source/'DOSCRAFT.ISO').read_bytes()
        if digest(iso)!=boot_record['iso_sha256']:raise ValueError('Shared ISO checksum mismatch')
        (result/'boot.img').write_bytes(boot);(result/'DOSCRAFT.ISO').write_bytes(iso)
    proof.update(source_vm=str(vm),source_disk_sha256=digest(original),disk_sha256=digest(disk),
                 save_before_sha256=digest(fs.read('WORLD.SAV')),save_after_sha256=digest(save))
    updated={**record,'vm_directory':str(result),'initial_hdd_sha256':digest(disk),
             'terrain_bytes':len(compact),'terrain_sha256':digest(compact),'terrain_format':'V2 RLE',
             'rle_conversion':proof,'source_terrain_sha256':digest(terrain)}
    if boot_from:
        for key in ('exe_sha256','boot_sha256','iso_sha256'):updated[key]=boot_record[key]
        for key in ('audio','sound_bank'):
            if key in boot_record:updated[key]=boot_record[key]
    if 'terrain_header_sha256' in updated:updated['terrain_header_sha256']=digest(compact[:97*512])
    (result/'build.json').write_text(json.dumps(updated,indent=2)+'\n')
    if running(vm) or source.read_bytes()!=original or Path(pointer.read_text().strip()).resolve()!=vm:
        raise ValueError('Player changed; converted clone staged but pointer unchanged')
    atomic_write(pointer,str(result).encode('utf-8'))
    return {'vm':str(result),'status':status,**proof}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pointer',type=Path,required=True)
    p.add_argument('--boot-from',type=Path,help='Tested shared release VM with the V2 reader')
    p.add_argument('--pool-pairs',type=int,default=DEFAULT_POOL_PAIRS,help='Edit capacity; 4096 retains full V1 capacity')
    args=p.parse_args()
    if not 1<=args.pool_pairs<=4096:p.error('Pool pairs must be between 1 and 4096')
    print(json.dumps(install(args.pointer,args.boot_from,args.pool_pairs),indent=2))


if __name__=='__main__':main()
