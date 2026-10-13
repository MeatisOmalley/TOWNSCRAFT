"""Prepare or launch a separate June 1990 tree experiment; preserve the release."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from build_game import ROOT,OUTPUT
from game_media import prepare,validate,large_volume,sha
from prepare_large_vm import attach_large_disk
from large_world_media import upgrade_disk
from publish_game import running

PREVIEW=ROOT/'build/checks/tree-canopies'

def prepare_preview(terrain,output,smoke,preview=PREVIEW):
    preview=Path(preview).resolve()
    if not preview.is_relative_to(ROOT/'build/checks'):raise ValueError('Preview output must stay under build/checks')
    player=Path((OUTPUT/'latest-play-vm.txt').read_text().strip()).resolve(strict=True)
    smoke=Path(smoke).resolve(strict=True);output=Path(output).resolve(strict=True)
    report=json.loads((smoke/'large-world-smoke.json').read_text())
    tested=json.loads((smoke/'build.json').read_text());build=json.loads((output/'build.json').read_text())
    terrain=Path(terrain).resolve(strict=True)
    if not tested.get('smoke') or not tested.get('large_world') or tested['exe_sha256']!=build['exe_sha256'] or \
       tested['terrain_sha256']!=sha(terrain.read_bytes()) or report.get('save_version')!=5 or \
       any('RESULT=PASS\n' not in report.get(mode,'') for mode in ('new','load')):
        raise ValueError('Need successful new/reload smoke on this exact tree build and seed')
    if running(player):raise ValueError('Close the player before copying its writable disk')
    original=(player/'scratch-large.img').read_bytes();original_hash=sha(original)
    vm,record=prepare(output=output,hardware_vm=player,update_pointer=False)
    attach_large_disk(vm,terrain,player=True)
    migrated=upgrade_disk(original,terrain.read_bytes())
    old=large_volume(original);new=large_volume(migrated)
    for name,attr,cluster,size in old.directory():
        if not attr&16 and name!='TERRAIN.TMP' and old.read(name)!=new.read(name):
            raise ValueError('Preview did not preserve '+name)
    if running(player) or sha((player/'scratch-large.img').read_bytes())!=original_hash:
        raise ValueError('Player changed during the copy')
    (vm/'scratch-large.img').write_bytes(migrated)
    record=json.loads((vm/'build.json').read_text())
    record.update(experiment='natural-tree-canopies' if preview==PREVIEW else preview.name,preserved_player_vm=str(player),
                  preserved_hdd_sha256=original_hash,verified_smoke_vm=str(smoke))
    (vm/'build.json').write_text(json.dumps(record,indent=2)+'\n');validate(vm)
    preview.mkdir(parents=True,exist_ok=True);(preview/'player-vm.txt').write_text(str(vm))
    script=ROOT/'tools/tree_canopy_preview.py'
    filename='Play-Capped-Trees.cmd' if preview.name=='capped-trees' else 'Play-Tree-Preview.cmd'
    (preview/filename).write_text('@echo off\n"'+str(Path(sys.executable))+
        '" "'+str(script)+'" --preview-directory "'+str(preview)+'" --launch\nif errorlevel 1 pause\n')
    return vm

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--terrain',type=Path);parser.add_argument('--build-output',type=Path)
    parser.add_argument('--smoke-vm',type=Path);parser.add_argument('--launch',action='store_true')
    parser.add_argument('--preview-directory',type=Path,default=PREVIEW)
    args=parser.parse_args()
    if args.terrain or args.build_output or args.smoke_vm:
        if not all((args.terrain,args.build_output,args.smoke_vm)):parser.error('Preparation requires all three paths')
        vm=prepare_preview(args.terrain,args.build_output,args.smoke_vm,args.preview_directory)
    else:vm=Path((args.preview_directory/'player-vm.txt').read_text().strip()).resolve(strict=True)
    if not vm.is_relative_to(ROOT/'runtime'):raise ValueError('Unknown preview VM')
    if running(vm):raise ValueError('Tree preview is already running')
    command=validate(vm);print('TREE_PREVIEW='+str(vm))
    if args.launch:subprocess.Popen(command,cwd=vm)

if __name__=='__main__':main()
