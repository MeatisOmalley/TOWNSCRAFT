"""Collect the 1996 benchmark reports (tools/benchmark_1996.py) into one
archive with the raw frame samples, and print the PERFORMANCE.md tables.

Each label/variant directory under build/bench/p5-200 points to its latest
report. Draw-profiling runs are kept apart: their timings are diagnostic.
"""
import argparse
import json
from pathlib import Path
import statistics

from build_game import ROOT
from frame_metrics import frame_metrics
from performance_compare import compact

BENCH = ROOT / 'build/bench/p5-200'
SCENES = {'open_static': 'still', 'walk': 'walk', 'walk_down': 'looking down',
          'walk_up': 'clouds (walk_up)', 'house': 'house', 'block_edits': 'edits'}
RENDER = ('sky', 'draw', 'finish')


def load(label, variant):
    pointer = BENCH / label / variant / 'latest-report.txt'
    if not pointer.exists():
        return None
    return json.loads(Path(pointer.read_text().strip()).read_text())


def scene_rows(runs):
    """Per scene: average FPS over all repeats, mean frame, render and present ms."""
    rows = {}
    for i, scene in enumerate(runs[0]['scenes']):
        same = [run['scenes'][i] for run in runs]
        frames = [x for row in same for x in row['frames']]
        rows[scene['name']] = {
            'fps': frame_metrics(frames),
            'mean_ms': statistics.mean(frames) / 1000,
            'render_ms': statistics.mean(sum(row['stage_mean_ms'][k] for k in RENDER) for row in same),
            'present_ms': statistics.mean(row['stage_mean_ms']['present'] for row in same),
            'wait_ms': statistics.mean(row['transport_mean_ms']['wait'] for row in same),
            'stage_mean_ms': {k: statistics.mean(row['stage_mean_ms'][k] for row in same)
                              for k in same[0]['stage_mean_ms']}}
    return rows


def summarize(report):
    if report['quality'] == 'sweep':
        return {q: scene_rows([run]) for q, run in zip(('low', 'medium', 'high'), report['results'])}
    return {report['quality']: scene_rows(report['results'])}


def table(title, columns):
    """columns: [(heading, {scene: row})]; prints average FPS and 1% low."""
    print(f'\n{title}\n')
    print('| Scene | ' + ' | '.join(h for h, _ in columns) + ' |')
    print('| --- | ' + ' | '.join('---:' for _ in columns) + ' |')
    for key, name in SCENES.items():
        cells = []
        for _, rows in columns:
            row = rows.get(key) if rows else None
            cells.append('n/a' if row is None else
                         f"{row['fps']['average_fps']:.1f} ({row['fps']['1_percent_low_fps']:.1f})")
        print(f'| {name} | ' + ' | '.join(cells) + ' |')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    args = parser.parse_args()
    archive = {'note': 'Pentium 200 / 430HX / Matrox Millennium / Voodoo Graphics VM (86Box fork '
                       '86box-doscraft-1996). Guest PIT timings of the frozen replay; Voodoo fill is '
                       'not timed by 86Box, see voodoo_fill_bound.', 'runs': {}}
    summaries = {}
    for label in sorted(p.name for p in BENCH.iterdir() if p.is_dir() and not p.name.startswith('check')):
        for variant in ('ref', 'soft', '3dfx'):
            report = load(label, variant)
            if report is None:
                continue
            key = f'{label}/{variant}'
            profiling = any(run['header'].get('draw_profiling') for run in report['results'])
            entry = {'variant': variant, 'quality': report['quality'], 'far_field': report['far_field'],
                     'voodoo': report['voodoo'], 'draw_profiling': profiling,
                     'summary': summarize(report)}
            if 'voodoo_fill_bound' in report:
                entry['voodoo_fill_bound'] = report['voodoo_fill_bound']
            if not profiling:
                entry['raw'] = compact(report)
            archive['runs'][key] = entry
            summaries[key] = entry
    args.archive.parent.mkdir(parents=True, exist_ok=True)
    args.archive.write_text(json.dumps(archive, indent=1) + '\n')

    def rows(key, quality):
        entry = summaries.get(key)
        return entry['summary'].get(quality) if entry else None

    def bound(key, quality):
        entry = summaries.get(key)
        if not entry or 'voodoo_fill_bound' not in entry:
            return None
        b = entry['voodoo_fill_bound'].get(quality) or {}
        return {name: {'fps': {'average_fps': v['fps_bound_overlapped'],
                               '1_percent_low_fps': v['fps_bound_serialized']}} for name, v in b.items()}

    table('Same picture size and view (320x200 logic, 8 blocks, distant off): average FPS (1% low)', [
        ('D96REF High', rows('same-res/ref', 'high')),
        ('D96SOFT Low', rows('same-res/soft', 'low')),
        ('D963DFX Low, CPU side', rows('same-res/3dfx', 'low')),
        ('D963DFX Low, fill bound (overlapped / serialized)', bound('same-res/3dfx', 'low'))])
    for quality in ('low', 'medium', 'high'):
        table(f'1996 presets, {quality}: average FPS (1% low)', [
            ('D96SOFT', rows('sweep/soft', quality)),
            ('D963DFX CPU side', rows('sweep/3dfx', quality)),
            ('D963DFX fill bound (overlapped / serialized)', bound('sweep/3dfx', quality))])
    table('Startup default (High, 16 blocks, distant High): average FPS (1% low)', [
        ('D96SOFT', rows('default/soft', 'high')),
        ('D963DFX CPU side', rows('default/3dfx', 'high')),
        ('D963DFX fill bound (overlapped / serialized)', bound('default/3dfx', 'high'))])
    print('\nRender CPU ms (sky+draw+finish) / present ms, same-picture runs:\n')
    for key in ('same-res/ref', 'same-res/soft', 'same-res/3dfx'):
        entry = summaries.get(key)
        if not entry:
            continue
        quality = next(iter(entry['summary']))
        print(key, {SCENES[k]: (round(v['render_ms'], 2), round(v['present_ms'], 2))
                    for k, v in entry['summary'][quality].items()})
    print(f'\nARCHIVE={args.archive}')


if __name__ == '__main__':
    main()
