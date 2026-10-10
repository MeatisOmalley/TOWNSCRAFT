"""Build/autoboot three guest graphics replays; no host keyboard needed.

Every run gets a NEW private HDD/VM. Never touches the player's ISO or disk.
Completion files are written after DOS storage/IRQ/display shutdown; only then
is this exact spawned emulator process terminated. A timeout leaves it open
for diagnosis rather than killing a potentially active guest disk operation.
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import time
from build_game import build, ROOT
from game_media import prepare, validate
from run_vm import diagnostic_volume
from frame_metrics import frame_metrics

OUTPUT=ROOT/'build/bench/486dx25'
SECTIONS=('game','world_update','collect','entities','flip_wait','sky','draw','finish','hud','present')


def parse(text):
    header={}; scenes=[]; row=header
    for line in text.splitlines():
        key,value=line.split('=',1)
        if key=='SCENE':
            row={'name':value}; scenes.append(row)
        elif key in ('FRAMES','SECTIONS','EDIT_CALLS'):
            row[key.lower()]=[int(x) for x in value.split(',')]
        else: row[key.lower()]=value
    if header.get('result')!='PASS' or len(scenes)!=6:
        raise ValueError('Incomplete/failed guest benchmark')
    count=int(header.get('samples_per_scene',32))
    if not 32<=count<=10000: raise ValueError('Invalid samples per scene')
    for row in scenes:
        frames=row['frames']
        if len(frames)!=count or not all(0<x<10000000 for x in frames) or len(row['sections'])!=10:
            raise ValueError('Invalid guest frame samples')
        ordered=sorted(frames)
        row['summary_ms']={
            'mean':statistics.mean(frames)/1000,'median':statistics.median(frames)/1000,
            'p95':ordered[(95*count+99)//100-1]/1000,
            'p99':ordered[(99*count+99)//100-1]/1000,
            'p99_9':ordered[(999*count+999)//1000-1]/1000,'max':max(frames)/1000}
        row['fps']=frame_metrics(frames)
        row['stage_mean_ms']={name:value/(count*1000) for name,value in zip(SECTIONS,row['sections'])}
        row['transport_mean_ms']={name:int(row[name])/(count*1000) for name in ('pack','upload','wait')}
        row['over_100ms']=sum(x>100000 for x in frames)
        if header.get('version')=='2':
            calls=row.get('edit_calls',[])
            if len(calls)!=count or any(x<0 or x>=10000000 for x in calls):
                raise ValueError('Invalid edit-call samples')
            if row['name']!='block_edits' and any(calls):
                raise ValueError('Edit calls in a non-edit scene')
            cycles=[a+b for a,b in zip(frames,calls)]
            row['edit_call_mean_ms']=statistics.mean(calls)/1000
            row['edit_cycle_summary_ms']={'mean':statistics.mean(cycles)/1000,
                'p95':sorted(cycles)[(95*count+99)//100-1]/1000,'max':max(cycles)/1000}
            row['edit_cycle_fps']=frame_metrics(cycles)
    return {'header':header,'scenes':scenes}


def collect(vm):
    volume=diagnostic_volume((vm/'scratch.img').read_bytes())
    results=[]
    for n in range(1,4):
        payload=volume.read(f'BENCH{n}.TXT')
        results.append(parse(payload.decode('ascii')))
    return results


def write_report(vm,output,manifest,build_record,label,results,archive=None):
    report={'manifest':manifest,'build':build_record,'results':results,'note':
        'Guest PIT timings; frozen deterministic graphics/update replay, not full simulation FPS.',
        'label':label,
        'fps_by_scene':{results[0]['scenes'][i]['name']:frame_metrics(
            [x for run in results for x in run['scenes'][i]['frames']]) for i in range(6)},
        'fps_overall':frame_metrics([x for run in results for scene in run['scenes'] for x in scene['frames']])}
    (vm/'performance.json').write_text(json.dumps(report,indent=2)+'\n')
    (output/'latest-report.txt').write_text(str(vm/'performance.json'))
    if archive:
        from performance_compare import compact
        archive=Path(archive)
        archive.parent.mkdir(parents=True,exist_ok=True)
        archive.write_text(json.dumps(compact(report),indent=2)+'\n')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bios',type=Path)
    parser.add_argument('--collect-vm',type=Path,help='Collect a finished private benchmark without launching it')
    parser.add_argument('--run-vm',type=Path,help='Run an untouched private benchmark prepared with --prepare-only')
    parser.add_argument('--label',default='baseline')
    parser.add_argument('--candidate',choices=('reference','pack-all','hud-cache','broadcast','pack-words','pack-words-c','omit-frame','et4000-chunky','et4000-strided','et4000-hud-words','et4000-strided-hud-words','trap-zero-gradient','trap-destination','trap-stable','trap-four','wall-visibility','split-reuse','depth-adaptive','occlusion','zero-dx','camera-reuse','packed-raster','early-split-cull','front-to-back','front-to-back-fast','edge-recip'),default='reference')
    parser.add_argument('--timeout',type=int,help='Host timeout seconds; default 900 for short runs, increased for long runs')
    parser.add_argument('--quality',choices=('low','medium','high'),default='low')
    parser.add_argument('--frames',type=int,default=32,help='Measured frames per scene per repeat (32..10000); 10000 recommended for tail metrics')
    parser.add_argument('--prepare-only',action='store_true')
    parser.add_argument('--verify-f2b',action='store_true',help='Redraw every frame with the original painter; correctness only, timings invalid')
    parser.add_argument('--verify-wall',action='store_true',help='Double-render every frame without wall rejection; correctness only, timings invalid')
    parser.add_argument('--f2b-stress',action='store_true',help='Verification-only cutout/model/overflow stress fixtures')
    parser.add_argument('--profile-draw',action='store_true',help='Guest IRQ0 EIP samples during drawing; diagnostic timings only')
    parser.add_argument('--dump-frames',action='store_true',help='Capture final scene frames to the private HDD; diagnostic timings only')
    parser.add_argument('--archive',type=Path,help='Retain compact raw evidence and source recipes in a tracked report path')
    args=parser.parse_args()
    if not 32<=args.frames<=10000: parser.error('--frames must be between 32 and 10000')
    if args.timeout is None: args.timeout=900 if args.frames==32 else 900+args.frames*6
    output=OUTPUT/args.label
    if args.collect_vm:
        vm=args.collect_vm.resolve(strict=True)
        manifest=json.loads((vm/'build.json').read_text())
        build_record=json.loads((output/'build.json').read_text())
        if not manifest.get('benchmark') or manifest['exe_sha256']!=build_record['exe_sha256']:
            raise ValueError('Collect only a matching private benchmark build')
        write_report(vm,output,manifest,build_record,args.label,collect(vm),args.archive)
        print(f'REPORT={vm / "performance.json"}')
        return
    variants={'reference':(), 'pack-all':('-DDOS_VGA_PACK_ALL',),
        'hud-cache':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE'),
        'broadcast':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST'),
        'pack-words':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_VGA_PACK_WORDS'),
        'pack-words-c':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_VGA_PACK_WORDS','-DDOS_VGA_PACK_WORDS_C_TRANSPOSE'),
        'omit-frame':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer'),
        'et4000-chunky':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_ET4000_CHUNKY'),
        'et4000-strided':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_ET4000_CHUNKY','-DDOS_ET4000_STRIDED'),
        'et4000-hud-words':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_ET4000_CHUNKY','-DDOS_ET4000_HUD_WORDS'),
        'et4000-strided-hud-words':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_ET4000_CHUNKY','-DDOS_ET4000_STRIDED','-DDOS_ET4000_HUD_WORDS'),
        'trap-zero-gradient':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_TRAP_ZERO_GRADIENT'),
        'trap-destination':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_TRAP_DESTINATION'),
        'split-reuse':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_SPLIT_REUSE'),
        'depth-adaptive':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_DEPTH_ADAPTIVE'),
        'trap-stable':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_TRAP_STABLE'),
        'trap-four':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_TRAP_FOUR'),
        'wall-visibility':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-fomit-frame-pointer','-DDOS_WALL_VISIBILITY'),
        'occlusion':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_BENCH_OCCLUSION'),
        'zero-dx':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_RASTER_ZERO_DX'),
        'camera-reuse':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_CAMERA_REUSE'),
        'packed-raster':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_PACKED_RASTER'),
        'early-split-cull':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_EARLY_SPLIT_CULL'),
        'front-to-back':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_FRONT_TO_BACK'),
        'front-to-back-fast':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_FRONT_TO_BACK','-DDOS_F2B_FAST_COVERAGE'),
        'edge-recip':('-DDOS_VGA_PACK_ALL','-DDOS_VGA_HUD_CACHE','-DDOS_VGA_BROADCAST','-DDOS_EDGE_RECIP')}
    flags=(*variants[args.candidate],'-DDOS_BENCH_QUALITY='+str(('low','medium','high').index(args.quality)))
    if args.frames!=32: flags=(*flags,'-DDOS_BENCH_FRAMES='+str(args.frames))
    if args.verify_f2b:
        if args.candidate not in ('front-to-back','front-to-back-fast'): parser.error('--verify-f2b needs a front-to-back candidate')
        flags=(*flags,'-DDOS_F2B_VERIFY')
    if args.verify_wall:
        if args.candidate!='wall-visibility': parser.error('--verify-wall needs the wall-visibility candidate')
        flags=(*flags,'-DDOS_WALL_VERIFY')
    if args.f2b_stress:
        if not args.verify_f2b: parser.error('--f2b-stress needs --verify-f2b')
        flags=(*flags,'-DDOS_F2B_STRESS')
    if args.profile_draw: flags=(*flags,'-DDOS_BENCH_PROFILE_DRAW')
    if args.dump_frames: flags=(*flags,'-DDOS_BENCH_DUMP_FRAMES')
    if args.run_vm:
        vm=args.run_vm.resolve(strict=True)
        manifest=json.loads((vm/'build.json').read_text())
        if not manifest.get('benchmark') or not vm.is_relative_to(ROOT/'runtime'):
            raise ValueError('Only a private prepared benchmark can be run')
        volume=diagnostic_volume((vm/'scratch.img').read_bytes())
        try: volume.read('BENCH1.TXT')
        except FileNotFoundError: pass
        else: raise ValueError('Already used benchmark disk; prepare a fresh VM')
    else:
        if not args.bios: parser.error('--bios is required to create a new benchmark VM')
        build(output,benchmark=True,extra_adapter_flags=flags)
        vm,manifest=prepare(bios=args.bios,output=output,benchmark=True)
    build_record=json.loads((output/'build.json').read_text())
    if manifest['exe_sha256']!=build_record['exe_sha256']:
        raise ValueError('Prepared VM and build manifest differ')
    command=validate(vm)
    print(f'VM={vm}',flush=True)
    if args.prepare_only: return
    # Exclusive launch claim: never open this private writable HDD twice.
    # Retain the claim after completion or timeout; fresh repetitions use a
    # fresh VM. --collect-vm may still collect preserved completed reports.
    with (vm/'benchmark-launched.json').open('x') as claim:
        json.dump({'command':command,'label':args.label},claim)
    process=subprocess.Popen(command,cwd=vm)
    print(f'PID={process.pid}',flush=True)
    until=time.monotonic()+args.timeout
    while time.monotonic()<until:
        volume=diagnostic_volume((vm/'scratch.img').read_bytes())
        for n in range(1,4):
            try: marker=volume.read(f'BENCH{n}.END')
            except (FileNotFoundError,ValueError): continue # FAT may be mid-update in a live snapshot.
            if marker==b'FAIL\n':
                process.terminate(); process.wait(timeout=30)
                raise RuntimeError(f'Completed guest benchmark {n} FAILED; reports preserved in {vm}')
        try: completed=volume.read('BENCH3.END')==b'PASS\n'
        except (FileNotFoundError,ValueError): completed=False
        if not completed:
            if process.poll() is not None: raise RuntimeError('Emulator exited before completion marker')
            time.sleep(2); continue
        try: results=collect(vm)
        except (ValueError,KeyError,FileNotFoundError,UnicodeDecodeError):
            if process.poll() is not None: raise RuntimeError('Emulator exited before reports')
            time.sleep(2); continue
        # Report completion means guest is back at an idle DOS prompt with
        # closed files and restored hardware. Process identity comes directly
        # from our Popen, never a name-based kill of unrelated user windows.
        process.terminate(); process.wait(timeout=30)
        write_report(vm,output,manifest,build_record,args.label,results,args.archive)
        for scene in range(6):
            rows=[r['scenes'][scene] for r in results]
            print(rows[0]['name'], 'mean_ms=',round(statistics.mean(r['summary_ms']['mean'] for r in rows),3),
                  'transport=',rows[0]['transport_mean_ms'],flush=True)
            print('FPS=',frame_metrics([x for r in rows for x in r['frames']]),flush=True)
        print(f'REPORT={vm / "performance.json"}',flush=True)
        return
    raise RuntimeError(f'No complete reports by timeout. Private VM {vm}, PID {process.pid} left for diagnosis.')


if __name__=='__main__': main()
