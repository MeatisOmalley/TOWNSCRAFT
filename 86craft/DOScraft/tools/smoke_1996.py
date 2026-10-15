"""In-emulator smoke of D96REF, D96SOFT and D963DFX on a NEW private 1996 VM.

Each executable runs the release's scripted /SMOKE (new world, walk, look,
torch place/break, inventory and menus, VGA readback, save) and /SMOKELOAD
(fresh-process reload of that save). Reports land on the private HDD as
C:\\<TAG>NEW.TXT and C:\\<TAG>LOAD.TXT. The emulator window is captured
read-only (PrintWindow) every few seconds into <vm>/captures so the Voodoo
output and its HUD/menu compositing can be inspected afterwards.

With --voodoo the 3dfx configuration is used (the normal case: all three
executables then run on one machine with the board present). Without it,
D963DFX must refuse cleanly and the other two must pass.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time

import vm_1996
from game_media import large_volume
from leaf_flags_media import SAVE_VERSION

TAGS = {'ref': 'REF', 'soft': 'SOFT', '3dfx': '3DFX'}
CAPTURE = Path(__file__).with_name('capture_window.ps1')


def capture(pid, out):
    return subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                           str(CAPTURE), '-ProcessId', str(pid), '-Out', str(out)],
                          capture_output=True, text=True).returncode == 0


def close(process, vm):
    """Graceful close of this exact private emulator once the guest is idle."""
    rows = vm_1996.running(vm)
    if not any(int(row['ProcessId']) == process.pid for row in rows):
        raise RuntimeError('Emulator identity changed; nothing closed')
    subprocess.run(['powershell.exe', '-NoProfile', '-Command',
                    f'$p=Get-Process -Id {process.pid}; [void]$p.CloseMainWindow(); $p.WaitForExit(30000)'],
                   check=True)
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=30)


def read(volume, name):
    try:
        return volume.read(name).decode('ascii')
    except (FileNotFoundError, ValueError):
        return ''


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cmos', type=Path, required=True)
    parser.add_argument('--voodoo', action='store_true')
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--interval', type=float, default=3.0, help='Seconds between window captures')
    args = parser.parse_args()
    vm, record = vm_1996.prepare('smoke', args.cmos)
    command = vm_1996.validate(vm, args.voodoo)
    (vm / 'captures').mkdir()
    process = subprocess.Popen(command, cwd=vm)
    print(f'VM={vm}\nPID={process.pid}', flush=True)
    deadline = time.monotonic() + args.timeout
    shot = 0
    try:
        while time.monotonic() < deadline:
            if capture(process.pid, vm / 'captures' / f'{shot:04d}.png'):
                shot += 1
            try:
                volume = large_volume((vm / vm_1996.DISK).read_bytes())
                done = read(volume, 'SMOKE.END').startswith('PASS')
            except ValueError:
                done = False
            if done:
                break
            if process.poll() is not None:
                raise RuntimeError('Emulator exited before the smoke finished')
            time.sleep(args.interval)
        else:
            raise TimeoutError(f'Smoke timed out; VM {vm} left open for diagnosis')
        time.sleep(2)
        volume = large_volume((vm / vm_1996.DISK).read_bytes())
        results = {}
        for name, tag in TAGS.items():
            new, load = read(volume, f'{tag}NEW.TXT'), read(volume, f'{tag}LOAD.TXT')
            results[name] = {'new': new, 'load': load,
                             'new_pass': 'RESULT=PASS\n' in new, 'load_pass': 'RESULT=PASS\n' in load}
        save = volume.read('WORLD.SAV')
        report = {'vm': str(vm), 'voodoo': args.voodoo, 'captures': shot, 'results': results,
                  'save_version_committed': any(save[at:at + 8] == b'TSCF' + SAVE_VERSION.to_bytes(4, 'little')
                                                for at in (0, len(save) // 2))}
        (vm / 'smoke-1996.json').write_text(json.dumps(report, indent=2) + '\n')
        close(process, vm)
        for name, row in results.items():
            print(f'{name}: NEW={"PASS" if row["new_pass"] else "FAIL"} LOAD={"PASS" if row["load_pass"] else "FAIL"}')
        print(f'REPORT={vm / "smoke-1996.json"}')
    except BaseException:
        print(f'Smoke incomplete; private VM preserved: {vm}', flush=True)
        raise


if __name__ == '__main__':
    main()
