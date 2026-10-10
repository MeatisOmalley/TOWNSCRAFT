"""Compare fixed-settings three-repeat guest reports; retain raw evidence."""
import argparse
import json
from pathlib import Path
import statistics


def compare(baseline,candidate,allowed_settings=()):
    if set(allowed_settings)-{'occlusion'}: raise ValueError('Unsupported setting change')
    for report in (baseline,candidate):
        if len(report['results'])!=3: raise ValueError('Need three guest repeats')
        if any(r['header'].get('f2b_verification') for r in report['results']):
            raise ValueError('Reference-redraw verification timings are not performance evidence')
        if any(r['header'].get('draw_profiling') for r in report['results']):
            raise ValueError('Sampling diagnostic timings are not performance evidence')
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
                keys=('version','seed','simulation','scale','view','flat','subdiv','occlusion')
                if baseline['results'][0]['header']['version']=='2': keys+=('edit_timing',)
                for key in keys:
                    if run['header'][key]!=report['results'][0]['header'][key]:
                        raise ValueError('Different settings within repeats: '+key)
                    if key not in allowed_settings and run['header'][key]!=baseline['results'][0]['header'][key]:
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
    result={'manifest':report['manifest'],'build':{key:report['build'].get(key) for key in
        ('source_commit','exe_sha256','benchmark','extra_adapter_flags','objects','link_command')},
        'results':report['results'],'note':report['note']}
    # Renderer overlays must retain staged-source recipes and object hashes,
    # not just adapter hashes/flags or the immutable upstream commit identity.
    imported=report['build'].get('imported')
    if imported:
        result['build']['imported']={key:imported.get(key) for key in
            ('source_commit','cflags','compiler_sha256','compiler_version','files','generated','objects')}
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--candidate',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--allow-occlusion-change',action='store_true',help='Explicit off/on investigation; image hashes must still match')
    args=parser.parse_args()
    baseline=json.loads(args.baseline.read_text()); candidate=json.loads(args.candidate.read_text())
    allowed=('occlusion',) if args.allow_occlusion_change else ()
    rows=compare(baseline,candidate,allowed)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps({'comparison':rows,'intentional_setting_changes':list(allowed),'baseline':compact(baseline),
        'candidate':compact(candidate)},indent=2)+'\n')
    for row in rows:
        print(f'{row["scene"]}: {row["baseline_ms"]:.3f} -> {row["candidate_ms"]:.3f} ms '
              f'({row["frame_time_reduction_percent"]:.1f}% reduction, matching image)')


if __name__=='__main__': main()
