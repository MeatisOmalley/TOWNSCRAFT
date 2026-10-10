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

OUTPUT=ROOT/'build/bench/486dx25'
SECTIONS=('game','world_update','collect','entities','flip_wait','sky','draw','finish','hud','present')


def parse(text):
    header={}; scenes=[]; row=header
    for line in text.splitlines():
        key,value=line.split('=',1)
        if key=='SCENE':
            row={'name':value}; scenes.append(row)
        elif key in ('FRAMES','SECTIONS'):
            row[key.lower()]=[int(x) for x in value.split(',')]
        else: row[key.lower()]=value
    if header.get('result')!='PASS' or len(scenes)!=6:
        raise ValueError('Incomplete/failed guest benchmark')
    for row in scenes:
        frames=row['frames']
        if len(frames)!=32 or not all(0<x<10000000 for x in frames) or len(row['sections'])!=10:
            raise ValueError('Invalid guest frame samples')
        ordered=sorted(frames)
        row['summary_ms']={
            'mean':statistics.mean(frames)/1000,'median':statistics.median(frames)/1000,
            'p95':ordered[30]/1000,'p99':ordered[31]/1000,'max':max(frames)/1000}
        row['stage_mean_ms']={name:value/32000 for name,value in zip(SECTIONS,row['sections'])}
        row['transport_mean_ms']={name:int(row[name])/32000 for name in ('pack','upload','wait')}
        row['over_100ms']=sum(x>100000 for x in frames)
    return {'header':header,'scenes':scenes}


def collect(vm):
    volume=diagnostic_volume((vm/'scratch.img').read_bytes())
    results=[]
    for n in range(1,4):
        payload=volume.read(f'BENCH{n}.TXT')
        results.append(parse(payload.decode('ascii')))
    return results


def write_report(vm,output,manifest,build_record,label,results):
    report={'manifest':manifest,'build':build_record,'results':results,'note':
        'Guest PIT timings; frozen deterministic graphics/update replay, not full simulation FPS.',
        'label':label}
    (vm/'performance.json').write_text(json.dumps(report,indent=2)+'\n')
    (output/'latest-report.txt').write_text(str(vm/'performance.json'))
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bios',type=Path)
    parser.add_argument('--collect-vm',type=Path,help='Collect a finished private benchmark without launching it')
    parser.add_argument('--label',default='baseline')
    parser.add_argument('--candidate',choices=('reference','pack-all'),default='reference')
    parser.add_argument('--timeout',type=int,default=900)
    parser.add_argument('--prepare-only',action='store_true')
    args=parser.parse_args()
    output=OUTPUT/args.label
    if args.collect_vm:
        vm=args.collect_vm.resolve(strict=True)
        manifest=json.loads((vm/'build.json').read_text())
        build_record=json.loads((output/'build.json').read_text())
        if not manifest.get('benchmark') or manifest['exe_sha256']!=build_record['exe_sha256']:
            raise ValueError('Collect only a matching private benchmark build')
        write_report(vm,output,manifest,build_record,args.label,collect(vm))
        print(f'REPORT={vm / "performance.json"}')
        return
    if not args.bios: parser.error('--bios is required to create a new benchmark VM')
    flags=('-DDOS_VGA_PACK_ALL',) if args.candidate=='pack-all' else ()
    build(output,benchmark=True,extra_adapter_flags=flags)
    build_record=json.loads((output/'build.json').read_text())
    vm,manifest=prepare(bios=args.bios,output=output,benchmark=True)
    command=validate(vm)
    print(f'VM={vm}',flush=True)
    if args.prepare_only: return
    process=subprocess.Popen(command,cwd=vm)
    print(f'PID={process.pid}',flush=True)
    until=time.monotonic()+args.timeout
    while time.monotonic()<until:
        volume=diagnostic_volume((vm/'scratch.img').read_bytes())
        for n in range(1,4):
            try: marker=volume.read(f'BENCH{n}.END')
            except FileNotFoundError: continue
            if marker==b'FAIL\n':
                process.terminate(); process.wait(timeout=30)
                raise RuntimeError(f'Completed guest benchmark {n} FAILED; reports preserved in {vm}')
        try: completed=volume.read('BENCH3.END')==b'PASS\n'
        except FileNotFoundError: completed=False
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
        write_report(vm,output,manifest,build_record,args.label,results)
        for scene in range(6):
            rows=[r['scenes'][scene] for r in results]
            print(rows[0]['name'], 'mean_ms=',round(statistics.mean(r['summary_ms']['mean'] for r in rows),3),
                  'transport=',rows[0]['transport_mean_ms'],flush=True)
        print(f'REPORT={vm / "performance.json"}',flush=True)
        return
    raise RuntimeError(f'No complete reports by timeout. Private VM {vm}, PID {process.pid} left for diagnosis.')


if __name__=='__main__': main()
