"""Frame-rate benchmark of the 1996 executables on the Pentium 200 VM.

The same frozen replay as tools/benchmark.py (six scenes: still, walk, looking
down, clouds, house, edits) in the published prebaked 1024 world, built as a
benchmark executable of one renderer variant and run on a NEW private 1996
VM: D96REF and D96SOFT on the Matrox-only configuration, D963DFX with the
Voodoo. Three repeats (/1 /2 /3) or one Low/Medium/High sweep (/SWEEP).

Guest PIT timings; the emulator process is closed only after the guest has
written its completion marker.
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import time

import build_1996
import vm_1996
from benchmark import parse
from build_game import ROOT
from frame_metrics import frame_metrics
from game_media import large_volume
from smoke_1996 import close

OUTPUT = ROOT / 'build/bench/p5-200'
QUALITY = ('low', 'medium', 'high', 'very-low')
# Always explicit: the 1996 builds start distant terrain at High, D96REF at Off.
FAR = {'off': 0, 'very-low': 1, 'low': 2, 'medium': 3, 'high': 4}


def flags(args):
    result = []
    if not args.sweep:
        result.append(f'-DDOS_BENCH_QUALITY={QUALITY.index(args.quality)}')
    else:
        result.append('-DDOS_BENCH_QUALITY=0')
    result.append(f'-DDOS_BENCH_FAR_LEVEL={FAR[args.far_field]}')
    if args.frames != 32:
        result.append(f'-DDOS_BENCH_FRAMES={args.frames}')
    return tuple(result)


def collect(vm):
    volume = large_volume((vm / vm_1996.DISK).read_bytes())
    return [parse(volume.read(f'BENCH{n}.TXT').decode('ascii')) for n in range(1, 4)]


def summarize(results, sweep):
    if sweep:
        return {quality: {scene['name']: scene['fps'] for scene in run['scenes']}
                for quality, run in zip(('low', 'medium', 'high'), results)}
    return {'fps_by_scene': {results[0]['scenes'][i]['name']: frame_metrics(
                [x for run in results for x in run['scenes'][i]['frames']]) for i in range(6)},
            'fps_overall': frame_metrics([x for run in results for scene in run['scenes']
                                          for x in scene['frames']])}


SST1_PIXELS_PER_SECOND = 50e6  # 50 MHz, one pixel per clock (peak)


def voodoo_fill_bound(results):
    """86Box rasterizes Voodoo triangles on host threads without the board's
    timing, so the guest frame times cover CPU work and PCI writes only.
    Bound each scene by the board's own pixel counts: rasterized pixels
    (fbiPixelsIn, chroma-keyed ones included: they take a clock too) plus
    fast-filled pixels, at one pixel per 50 MHz clock."""
    bound = {}
    for index in range(6):
        rows = [run['scenes'][index] for run in results]
        frames = sum(len(row['frames']) for row in rows)
        if not all('voodoo_pixels_in' in row for row in rows):
            return None
        pixels = sum(int(row['voodoo_pixels_in']) for row in rows) / frames
        fill = sum(int(row['voodoo_fastfill']) for row in rows) / frames
        fill_ms = (pixels + fill) / SST1_PIXELS_PER_SECOND * 1000
        cpu_ms = sum(sum(row['frames']) for row in rows) / frames / 1000
        bound[rows[0]['name']] = {
            'pixels_in_per_frame': round(pixels), 'fastfill_per_frame': round(fill),
            'fill_ms_per_frame': fill_ms, 'measured_ms_per_frame': cpu_ms,
            'fps_measured': 1000 / cpu_ms,
            'fps_bound_overlapped': 1000 / max(cpu_ms, fill_ms),
            'fps_bound_serialized': 1000 / (cpu_ms + fill_ms)}
    return bound


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('variant', choices=build_1996.VARIANTS)
    parser.add_argument('--cmos', type=Path, required=True)
    parser.add_argument('--label', default='baseline')
    parser.add_argument('--quality', choices=QUALITY, default='high')
    parser.add_argument('--sweep', action='store_true', help='Low, Medium, High in one process')
    parser.add_argument('--far-field', choices=tuple(FAR), default='off')
    parser.add_argument('--frames', type=int, default=32)
    parser.add_argument('--timeout', type=int)
    parser.add_argument('--voodoo', choices=('auto', 'on', 'off'), default='auto',
                        help='Board present? auto = only for D963DFX')
    args = parser.parse_args()
    if not 32 <= args.frames <= 10000:
        parser.error('--frames must be between 32 and 10000')
    timeout = args.timeout or 1200 + args.frames * 6
    voodoo = args.variant == '3dfx' if args.voodoo == 'auto' else args.voodoo == 'on'
    output = OUTPUT / args.label / args.variant
    build_record = build_1996.build(args.variant, output, benchmark=True, extra=flags(args))
    mode = 'sweep' if args.sweep else 'separate'
    vm, manifest = vm_1996.prepare('bench', args.cmos, builds={args.variant: output},
                                   variant=args.variant, benchmark=mode)
    command = vm_1996.validate(vm, voodoo)
    with (vm / 'benchmark-launched.json').open('x') as claim:
        json.dump({'command': command, 'label': args.label}, claim)
    process = subprocess.Popen(command, cwd=vm)
    print(f'VM={vm}\nPID={process.pid}', flush=True)
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        try:
            volume = large_volume((vm / vm_1996.DISK).read_bytes())
            markers = [volume.read(f'BENCH{n}.END') if any(e[0] == f'BENCH{n}.END' for e in volume.directory())
                       else None for n in range(1, 4)]
        except (ValueError, FileNotFoundError):
            markers = [None] * 3
        if b'FAIL\n' in markers:
            close(process, vm)
            raise RuntimeError(f'Guest benchmark FAILED; reports preserved in {vm}')
        if markers[2] == b'PASS\n':
            try:
                results = collect(vm)
            except (ValueError, KeyError, FileNotFoundError, UnicodeDecodeError):
                time.sleep(2)
                continue
            close(process, vm)
            break
        if process.poll() is not None:
            raise RuntimeError('Emulator exited before completion marker')
        time.sleep(3)
    else:
        raise RuntimeError(f'No complete reports by timeout. Private VM {vm}, PID {process.pid} left for diagnosis.')
    report = {'manifest': manifest, 'build': build_record, 'results': results,
              'variant': args.variant, 'voodoo': voodoo, 'label': args.label,
              'quality': 'sweep' if args.sweep else args.quality, 'far_field': args.far_field,
              'note': 'Guest PIT timings; frozen deterministic graphics/update replay, not full simulation FPS.',
              **summarize(results, args.sweep)}
    if args.variant == '3dfx':
        groups = (dict(zip(('low', 'medium', 'high'), ([run] for run in results))) if args.sweep
                  else {args.quality: results})
        report['voodoo_fill_bound'] = {q: voodoo_fill_bound(runs) for q, runs in groups.items()}
        for quality, bound in report['voodoo_fill_bound'].items():
            for name, row in (bound or {}).items():
                print(quality, name, 'fill_ms=', round(row['fill_ms_per_frame'], 3),
                      'fps measured/overlapped/serialized=', round(row['fps_measured'], 1),
                      round(row['fps_bound_overlapped'], 1), round(row['fps_bound_serialized'], 1), flush=True)
    (vm / 'performance.json').write_text(json.dumps(report, indent=2) + '\n')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'latest-report.txt').write_text(str(vm / 'performance.json'))
    for i in range(6):
        rows = [run['scenes'][i] for run in results]
        print(rows[0]['name'], 'mean_ms=', [round(r['summary_ms']['mean'], 3) for r in rows], flush=True)
    print(f'REPORT={vm / "performance.json"}')


if __name__ == '__main__':
    main()
