"""Compare fixed-settings three-repeat guest reports; retain raw evidence."""
import argparse
import json
from pathlib import Path
import statistics


def compare(baseline,candidate):
    for report in (baseline,candidate):
        if len(report['results'])!=3: raise ValueError('Need three guest repeats')
        if any(r['header']['result']!='PASS' for r in report['results']):
            raise ValueError('A failed guest run cannot establish a gain')
    for key in ('emulator_sha256','rom_sha256','profile_sha256','bios_seed_sha256'):
        if baseline['manifest'][key]!=candidate['manifest'][key]:
            raise ValueError('Different hardware/runtime: '+key)
    rows=[]
    for i,base_scene in enumerate(baseline['results'][0]['scenes']):
        old=[r['scenes'][i] for r in baseline['results']]
        new=[r['scenes'][i] for r in candidate['results']]
        for report in (baseline,candidate):
            for run in report['results']:
                for key in ('version','seed','simulation','scale','view','flat','subdiv','occlusion'):
                    if run['header'][key]!=baseline['results'][0]['header'][key]:
                        raise ValueError('Different replay/settings: '+key)
        if any(s['name']!=base_scene['name'] or s['hash']!=base_scene['hash'] for s in old+new):
            raise ValueError('Image mismatch: '+base_scene['name'])
        before=statistics.mean(r['summary_ms']['mean'] for r in old)
        after=statistics.mean(r['summary_ms']['mean'] for r in new)
        rows.append({'scene':base_scene['name'],'baseline_ms':before,'candidate_ms':after,
            'frame_time_reduction_percent':100*(before-after)/before,
            'baseline_repeat_means_ms':[r['summary_ms']['mean'] for r in old],
            'candidate_repeat_means_ms':[r['summary_ms']['mean'] for r in new],
            'baseline_p95_ms':[r['summary_ms']['p95'] for r in old],
            'candidate_p95_ms':[r['summary_ms']['p95'] for r in new],
            'hash':base_scene['hash']})
    return rows


def compact(report):
    return {'manifest':report['manifest'],'build':{key:report['build'].get(key) for key in
        ('source_commit','exe_sha256','benchmark','extra_adapter_flags','objects','link_command')},
        'results':report['results'],'note':report['note']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--candidate',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    baseline=json.loads(args.baseline.read_text()); candidate=json.loads(args.candidate.read_text())
    rows=compare(baseline,candidate)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps({'comparison':rows,'baseline':compact(baseline),
        'candidate':compact(candidate)},indent=2)+'\n')
    for row in rows:
        print(f'{row["scene"]}: {row["baseline_ms"]:.3f} -> {row["candidate_ms"]:.3f} ms '
              f'({row["frame_time_reduction_percent"]:.1f}% reduction, matching image)')


if __name__=='__main__': main()
