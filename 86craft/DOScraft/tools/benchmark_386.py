"""Run three deterministic replays on a private 386SX-25 compact-world VM.

Matches the 386 Very Low preset (80x50, four-block near distance) and Very Low
distant terrain. Game/world-update simulation is frozen, as in benchmark.py.
No host key injection or player-disk writes are involved.
"""
import argparse
import json
from pathlib import Path
import statistics
import struct
import subprocess
import time

from benchmark import parse, write_report
from build_game import ROOT, DEFAULT_ADAPTER_FLAGS, build
from fat_image import FatImage
from frame_metrics import frame_metrics
from prepare_386_vm import prepare, BIOS_SEED, EMULATOR, ROMS


def volume(vm):
    disk = (vm / 'hdd.img').read_bytes()
    return FatImage(disk, struct.unpack_from('<I', disk, 454)[0] * 512)


def stop_completed(process, vm):
    command = (f'Get-CimInstance Win32_Process -Filter "ProcessId = {process.pid}" | '
        'Select-Object ExecutablePath,CommandLine | ConvertTo-Json -Compress')
    text = subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', command], text=True)
    row = json.loads(text) if text.strip() else None
    if row is None and process.poll() is not None:
        return
    if row is None or str(row.get('ExecutablePath', '')).casefold() != str(EMULATOR).casefold() or \
            str(vm).casefold() not in str(row.get('CommandLine', '')).casefold():
        raise ValueError('Benchmark process identity changed; no process stopped')
    process.terminate()
    process.wait(timeout=30)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label', default='very-low-far-very-low')
    parser.add_argument('--frames', type=int, default=32)
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--terrain', type=Path, default=ROOT / 'build/checks/large-world-proto/terrain1024-crown-leaves.bin')
    parser.add_argument('--bios', type=Path, default=BIOS_SEED)
    parser.add_argument('--profile-cw', action='store_true', help='Instrument world/collection stages; diagnostic timings')
    parser.add_argument('--dump-frames', action='store_true', help='Save final scene images; diagnostic timings')
    parser.add_argument('--extra-flag', action='append', default=[], help='Build flag for an isolated experiment')
    parser.add_argument('--textured-distance', type=int, choices=range(17), help='Shared textured distance, including zero')
    parser.add_argument('--flat-distance', type=int, choices=range(4,17), help='Shared flat distance, minimum four')
    parser.add_argument('--single-repeat', action='store_true', help='Fresh-world diagnostic repeat, not a three-repeat gain comparison')
    parser.add_argument('--travel-step', type=int, choices=(512,1024,2048), help='Diagonal diagnostic route step in 1/4096 blocks')
    args = parser.parse_args()
    if not 32 <= args.frames <= 10000:
        parser.error('--frames must be between 32 and 10000')
    if Path(args.label).name != args.label or args.label in ('.', '..'):
        parser.error('--label must be a directory name')
    output = ROOT / 'build/bench/386sx25' / args.label
    flags = (*DEFAULT_ADAPTER_FLAGS, '-DDOS_LARGE_WORLD', '-DDOS_STREAM_LIGHT_CACHE',
        '-DDOS_FAR_FIELD', '-DDOS_FAR_DETAILS', '-DDOS_FAR_VIEW_DEPTH', '-DDOS_FAR_REPROJECT',
        '-DDOS_BENCH_QUALITY=3', '-DDOS_BENCH_RESOLUTION=3', '-DDOS_BENCH_FAR_LEVEL=1',
        '-DDOS_BENCH_PROFILE_FAR', '-DDOS_BENCH_FRAMES=' + str(args.frames))
    if args.profile_cw:
        flags = (*flags, '-DDOS_BENCH_PROFILE_CW')
    if args.dump_frames:
        flags = (*flags, '-DDOS_BENCH_DUMP_FRAMES')
    if args.flat_distance is not None and args.textured_distance is None:
        parser.error('--flat-distance requires --textured-distance')
    if args.textured_distance is not None:
        flags = (*flags, '-DDOS_BENCH_TEXTURED_DISTANCE=' + str(args.textured_distance))
    if args.flat_distance is not None:
        flags = (*flags, '-DDOS_BENCH_FLAT_DISTANCE=' + str(args.flat_distance))
    if args.travel_step is not None:
        flags = (*flags, '-DDOS_BENCH_DIAGONAL_ROUTE', '-DDOS_BENCH_TRAVEL_STEP=' + str(args.travel_step))
    flags = (*flags, *args.extra_flag)
    build_record = build(output=output, benchmark=True, extra_adapter_flags=flags)
    vm, manifest = prepare(args.terrain, build_output=output, bios=args.bios,
        benchmark='single' if args.single_repeat else True, update_pointer=False)
    repeats = 1 if args.single_repeat else 3
    command = [str(EMULATOR), '-P', str(vm), '-R', str(ROMS)]
    (vm / 'benchmark-launched.json').write_text(json.dumps({'command': command, 'label': args.label}, indent=2) + '\n')
    process = subprocess.Popen(command, cwd=vm)
    print(f'VM={vm}\nPID={process.pid}', flush=True)
    until = time.monotonic() + args.timeout
    while time.monotonic() < until:
        fs = volume(vm)
        markers = []
        for n in range(1, repeats + 1):
            try:
                markers.append(fs.read(f'BENCH{n}.END'))
            except (FileNotFoundError, ValueError):
                markers.append(None)
        if b'FAIL\n' in markers:
            stop_completed(process, vm)
            raise RuntimeError(f'Guest benchmark failed; reports preserved at {vm}')
        if markers[-1] == b'PASS\n':
            results = [parse(fs.read(f'BENCH{n}.TXT').decode('ascii')) for n in range(1, repeats + 1)]
            stop_completed(process, vm)
            report = write_report(vm, output, manifest, build_record, args.label, results)
            archive = output / 'performance.json'
            archive.write_text(json.dumps(report, indent=2) + '\n')
            print(f'OVERALL_FPS={report["fps_overall"]["average_fps"]:.3f}')
            for i in range(6):
                rows = [run['scenes'][i] for run in results]
                metrics = frame_metrics([x for row in rows for x in row['frames']])
                stages = {name: statistics.mean(row['stage_mean_ms'][name] for row in rows)
                    for name in rows[0]['stage_mean_ms']}
                print(rows[0]['name'], f'FPS={metrics["average_fps"]:.3f}',
                    f'MEAN_MS={1000 / metrics["average_fps"]:.3f}', 'STAGES=' + json.dumps(stages))
            print(f'REPORT={archive}', flush=True)
            return
        if process.poll() is not None:
            raise RuntimeError('Emulator exited before benchmark completion; private files preserved: ' + str(vm))
        time.sleep(2)
    raise RuntimeError(f'Benchmark timed out; private VM {vm}, PID {process.pid} left for diagnosis')


if __name__ == '__main__':
    main()
