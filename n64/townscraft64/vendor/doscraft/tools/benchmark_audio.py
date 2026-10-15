"""Matched 486DX/40, 8 MiB silent/busy-effects replay on private V2 worlds."""
import argparse
import json
from pathlib import Path
import subprocess
import time
from build_game import ROOT,build,DEFAULT_ADAPTER_FLAGS
from game_media import prepare,validate
from prepare_large_vm import attach_large_disk
from benchmark import benchmark_volume,parse,write_report


def stop(process,vm,command):
    if process.poll() is not None:return
    query=(f'Get-CimInstance Win32_Process -Filter "ProcessId = {process.pid}" | '
           'Select-Object ExecutablePath,CommandLine | ConvertTo-Json -Compress')
    row=json.loads(subprocess.check_output(['powershell.exe','-NoProfile','-Command',query],text=True))
    if str(row.get('ExecutablePath','')).casefold()!=str(command[0]).casefold() or str(vm).casefold() not in str(row.get('CommandLine','')).casefold():
        raise RuntimeError('Diagnostic process changed; no process stopped')
    process.terminate();process.wait(timeout=30)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--audio',choices=('off','busy'),required=True)
    p.add_argument('--label')
    args=p.parse_args();audio=args.audio=='busy'
    label=args.label or 'audio-'+args.audio+'-20261014'
    if Path(label).name!=label or label in ('.','..'):p.error('Label must be a directory name')
    output=ROOT/'build/bench/486dx25'/label
    flags=(*DEFAULT_ADAPTER_FLAGS,'-DDOS_LARGE_WORLD','-DDOS_STREAM_LIGHT_CACHE','-DDOS_FAR_FIELD',
           '-DDOS_FAR_DETAILS','-DDOS_FAR_VIEW_DEPTH','-DDOS_FAR_REPROJECT',
           '-DDOS_BENCH_QUALITY=0','-DDOS_BENCH_FAR_LEVEL=2','-DDOS_BENCH_PROFILE_FAR')
    if audio:flags=(*flags,'-DDOS_BENCH_AUDIO_STRESS')
    record=build(output=output,benchmark=True,extra_adapter_flags=flags,audio=audio)
    hardware=Path((ROOT/'build/game/486dx25/latest-play-vm.txt').read_text().strip())
    vm,manifest=prepare(output=output,benchmark=True,hardware_vm=hardware,update_pointer=False)
    attach_large_disk(vm,ROOT/'build/checks/large-world-proto/terrain1024-crown-leaves.bin',player=True)
    manifest=json.loads((vm/'build.json').read_text());command=validate(vm)
    process=subprocess.Popen(command,cwd=vm);print(f'VM={vm}\nPID={process.pid}',flush=True)
    end=time.monotonic()+900
    while time.monotonic()<end:
        fs=benchmark_volume(vm)
        markers=[]
        for i in range(1,4):
            try:markers.append(fs.read(f'BENCH{i}.END'))
            except FileNotFoundError:markers.append(None)
        if b'FAIL\n' in markers:
            stop(process,vm,command);raise RuntimeError('Failed guest report preserved: '+str(vm))
        if markers[-1]==b'PASS\n':
            results=[parse(fs.read(f'BENCH{i}.TXT').decode()) for i in range(1,4)]
            stop(process,vm,command)
            report=write_report(vm,output,manifest,record,label,results)
            (output/'performance.json').write_text(json.dumps(report,indent=2)+'\n')
            print('FPS',report['fps_overall']['average_fps'],'REPORT',output/'performance.json');return
        if process.poll() is not None:raise RuntimeError('Guest exited; private files preserved: '+str(vm))
        time.sleep(3)
    raise RuntimeError('Timed out; private diagnostic left open: '+str(vm))


if __name__=='__main__':main()
