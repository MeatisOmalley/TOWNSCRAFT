"""Measure the real DOS terrain transport on an isolated hardware/disk copy."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from build_game import ROOT
from fat_image import FatImage, populate
from game_media import game_command, large_volume, sha
from publish_game import running
from refresh_1992_vm import running as running_1992


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-output', required=True, type=Path)
    parser.add_argument('--hardware-vm', required=True, type=Path)
    parser.add_argument('--disk-vm', required=True, type=Path)
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--lighting',action='store_true',help='Measure complete column lighting through the actual world cache')
    args = parser.parse_args()
    output = args.build_output.resolve(strict=True)
    source = args.hardware_vm.resolve(strict=True)
    disk_source = args.disk_vm.resolve(strict=True)
    if not source.is_relative_to(ROOT/'runtime') or not disk_source.is_relative_to(ROOT/'runtime'):
        raise ValueError('Expected private DOScraft hardware/disk sources')
    if running(disk_source) or running_1992(disk_source):
        raise ValueError('Disk source must be stopped')
    build = json.loads((output/'build.json').read_text())
    record = json.loads((source/'build.json').read_text())
    vm = Path(tempfile.mkdtemp(prefix='game-stream-probe-', dir=ROOT/'runtime'))
    obj = vm/'probe.o'
    compiler = build['link_command'][0]
    flags=build.get('extra_adapter_flags',[]) if args.lighting else []
    probe_source=ROOT/'tests'/('dos_light_probe.c' if args.lighting else 'dos_stream_probe.c')
    subprocess.run([compiler,'-march=i486','-O2','-std=gnu99',*flags,'-I',str(output/'imported/src'),
                    '-I',str(ROOT/'src/platform/dos'),'-c',str(probe_source),'-o',str(obj)],check=True)
    command = [str(obj) if Path(part).name=='game_entry.o' else part for part in build['link_command']]
    if args.lighting: command=[part for part in command if Path(part).name!='world.o']
    command[-1] = str(vm/'PROBE.EXE')
    subprocess.run(command,check=True)
    boot = (source/'boot.img').read_bytes()
    files = FatImage(boot)
    payload = [(name,b'@ECHO OFF\r\nCWSDPMI -p -s-\r\nPROBE\r\n' if name=='AUTOEXEC.BAT'
                else files.read(name)) for name,attr,cluster,size in files.directory()]
    payload.append(('PROBE.EXE',(vm/'PROBE.EXE').read_bytes()))
    (vm/'boot.img').write_bytes(populate(boot[:512],payload))
    shutil.copy2(source/'86box.cfg',vm/'86box.cfg')
    shutil.copytree(source/'nvr',vm/'nvr')
    disk = vm/'scratch-large.img'
    shutil.copy2(disk_source/'scratch-large.img',disk)
    (vm/'build.json').write_text(json.dumps({**record,'smoke':True,'vm_directory':str(vm)},indent=2)+'\n')
    process = subprocess.Popen(game_command(vm,record),cwd=vm)
    print(f'VM={vm}\nPID={process.pid}',flush=True)
    deadline = time.monotonic()+args.timeout
    try:
        while time.monotonic()<deadline:
            try: report = large_volume(disk.read_bytes()).read('IOPROBE.TXT').decode('ascii')
            except (FileNotFoundError,ValueError): report = ''
            if report:
                (vm/'io-probe.txt').write_text(report)
                print(report,flush=True)
                if not report.startswith('RESULT=PASS\n'): raise RuntimeError('Guest disk probe failed')
                return
            if process.poll() is not None: raise RuntimeError('Probe exited without a report')
            time.sleep(5)
        raise TimeoutError('Guest disk probe timed out')
    finally:
        if process.poll() is None:
            process.terminate(); process.wait(timeout=30)


if __name__=='__main__': main()
