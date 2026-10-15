"""Run the opt-in 1024-world build on its private 1990 SCSI VM."""

import argparse
import json
from pathlib import Path
import subprocess
import struct
import time

from fat_image import FatImage
from game_media import game_command


def large_volume(disk):
    """Read the private large FAT16 disk rather than the 20 MB diagnostic disk."""
    if len(disk) != 800 * 8 * 63 * 512 or disk[510:512] != b'\x55\xaa':
        raise ValueError('Large-world HDD geometry mismatch')
    start, size = struct.unpack_from('<II', disk, 454)
    if disk[450] != 6 or start != 63 or start + size != len(disk) // 512:
        raise ValueError('Large-world FAT16 partition mismatch')
    volume = FatImage(disk, start * 512)
    if volume.bits != 16 or volume.total != size:
        raise ValueError('Large-world FAT16 extent mismatch')
    return volume


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vm', required=True, type=Path)
    parser.add_argument('--timeout', type=int, default=900)
    args = parser.parse_args()
    vm = args.vm.resolve(strict=True)
    record = json.loads((vm / 'build.json').read_text())
    if not record.get('smoke') or not record.get('large_world_experiment'):
        raise ValueError('Expected a private large-world smoke VM')
    disk = vm / 'scratch-large.img'
    process = subprocess.Popen(game_command(vm, record), cwd=vm)
    print(f'VM={vm}\nPID={process.pid}', flush=True)
    deadline = time.monotonic() + args.timeout
    try:
        while time.monotonic() < deadline:
            try:
                volume = large_volume(disk.read_bytes())
                first = volume.read('PLAYNEW.TXT').decode('ascii')
            except (FileNotFoundError, ValueError):
                first = ''
            if 'RESULT=FAIL\n' in first:
                raise RuntimeError(f'Large-world new-game smoke failed:\n{first}')
            if first and 'RESULT=PASS\n' in first:
                print('NEW=PASS', flush=True)
                try:
                    second = volume.read('PLAYLOAD.TXT').decode('ascii')
                except FileNotFoundError:
                    second = ''
                if 'RESULT=FAIL\n' in second:
                    raise RuntimeError(f'Large-world reload smoke failed:\n{second}')
                if 'RESULT=PASS\n' in second:
                    if 'WORLD_WIDTH=1024\n' not in first or 'WORLD_WIDTH=1024\n' not in second:
                        raise RuntimeError('Guest reported incorrect world width')
                    save = volume.read('WORLD.SAV')
                    if not any(save[at:at + 8] == b'TSCF\x05\x00\x00\x00'
                               for at in (0, len(save) // 2)):
                        raise RuntimeError('No committed V5 save bank')
                    (vm / 'large-world-smoke.json').write_text(json.dumps({
                        'new': first, 'load': second, 'save_version': 5}, indent=2) + '\n')
                    print('LOAD=PASS\nSAVE_V5=PASS', flush=True)
                    return
            if process.poll() is not None:
                raise RuntimeError('Large-world guest exited before both reports')
            time.sleep(5)
        raise TimeoutError('Large-world smoke timed out')
    finally:
        if process.poll() is None:
            query=(f'Get-CimInstance Win32_Process -Filter "ProcessId = {process.pid}" | '
                   'Select-Object ExecutablePath,CommandLine | ConvertTo-Json -Compress')
            row=json.loads(subprocess.check_output(['powershell.exe','-NoProfile','-Command',query],text=True))
            if (str(row.get('ExecutablePath','')).casefold()!=str(game_command(vm,record)[0]).casefold() or
                    str(vm).casefold() not in str(row.get('CommandLine','')).casefold()):
                raise RuntimeError('Diagnostic process identity changed; no process stopped')
            process.terminate()
            process.wait(timeout=30)


if __name__ == '__main__':
    main()
