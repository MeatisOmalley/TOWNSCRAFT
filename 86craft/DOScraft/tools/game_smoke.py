"""Build/boot the real game, scripted AT input, save/reload; no Windows keys."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from build_game import build,ROOT,OUTPUT,DEFAULT_ADAPTER_FLAGS
from game_media import prepare,validate
from run_vm import diagnostic_volume

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bios',required=True,type=Path)
    parser.add_argument('--timeout',type=int,default=900)
    parser.add_argument('--report',required=True,type=Path)
    parser.add_argument('--omit-frame-pointer',action='store_true',help='Test the explicit codegen candidate before changing production defaults')
    parser.add_argument('--et4000-chunky',action='store_true',help='Test the opt-in banked CPU upload, including graphics-preset transitions')
    args=parser.parse_args()
    flags=None
    if args.omit_frame_pointer or args.et4000_chunky:
        flags=tuple(dict.fromkeys((*DEFAULT_ADAPTER_FLAGS,
            *(('-fomit-frame-pointer',) if args.omit_frame_pointer else ()),
            *(('-DDOS_ET4000_CHUNKY',) if args.et4000_chunky else ()))))
    build(extra_adapter_flags=flags)
    vm,record=prepare(smoke=True,bios=args.bios)
    process=subprocess.Popen(validate(vm),cwd=vm)
    print(f'VM={vm}\nPID={process.pid}',flush=True)
    until=time.monotonic()+args.timeout
    while time.monotonic()<until:
        volume=diagnostic_volume((vm/'scratch.img').read_bytes())
        try: first=volume.read('PLAYNEW.TXT').decode('ascii')
        except FileNotFoundError: first=''
        if 'RESULT=FAIL\n' in first:
            process.terminate(); process.wait(timeout=30); raise RuntimeError(f'New-game smoke FAILED; {vm}')
        try: reports=[volume.read(name).decode('ascii') for name in ('PLAYNEW.TXT','PLAYLOAD.TXT')]
        except FileNotFoundError:
            if process.poll() is not None: raise RuntimeError('Guest stopped before smoke reports')
            time.sleep(2); continue
        if not all('RESULT=PASS\n' in report for report in reports):
            process.terminate(); process.wait(timeout=30); raise RuntimeError(f'Guest smoke FAILED; {vm}')
        # Guest closes storage/restores hardware before writing these reports.
        process.terminate(); process.wait(timeout=30)
        subprocess.run([sys.executable,str(ROOT/'tools/game_report.py'),'--vm',str(vm),
            '--output',str(args.report)],check=True,stdout=subprocess.DEVNULL)
        manifest=json.loads((OUTPUT/'build.json').read_text())
        if manifest['exe_sha256']!=record['exe_sha256']: raise ValueError('Build changed during smoke')
        manifest['smoke_verified_exe_sha256']=record['exe_sha256']
        manifest['smoke_report']=str((args.report/'report.json').resolve())
        (OUTPUT/'build.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print(f'PASS={args.report / "report.json"}',flush=True); return
    raise RuntimeError(f'Smoke timeout; private active VM {vm}, PID {process.pid} preserved for diagnosis')

if __name__=='__main__': main()
