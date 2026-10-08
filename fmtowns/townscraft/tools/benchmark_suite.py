"""Repeat the standard test on fixed profiles; keep every phase separate.

Example: python tools/benchmark_suite.py --refs 4edc6210 1f6d3626 39cf283e --repeats 3
Single step: add --step down. Higher profile: --profile high.
Results/logs live under ignored build/perf-audit. No production ISO is changed.
"""
import argparse
import concurrent.futures
import json
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PROFILES = {'minimum': (2,16,2,8), 'high': (4,25,1,12)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--refs', nargs='+', required=True, help='Exact Git refs, or WORKTREE')
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--profile', choices=PROFILES, default='minimum')
    p.add_argument('--step', choices=['all','look','walk','down','up','break','mixed'], default='all')
    p.add_argument('--width', type=int, default=80)
    p.add_argument('--mobs', type=int, default=0)
    p.add_argument('--jobs', type=int, default=2)
    a = p.parse_args()
    if a.repeats<1 or a.jobs<1: p.error('repeats/jobs must be positive')
    ram,freq,scale,view=PROFILES[a.profile]
    def run(job):
        index,ref,repeat=job
        name=f'suite-{a.profile}-{a.step}-w{a.width}-m{a.mobs}-{index}-{repeat}'
        out=ROOT/'build/perf-audit'/name
        out.mkdir(parents=True,exist_ok=True)
        args=[sys.executable,str(ROOT/'tools/perf_audit.py'),'--name',name,'--ref',ref,
              '--standard','--ram',str(ram),'--freq',str(freq),'--scale',str(scale),
              '--view',str(view),'--width',str(a.width),'--mobs',str(a.mobs),'--step',a.step]
        with (out/'suite.log').open('w') as log:
            subprocess.run(args,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        result=json.loads((out/'result.json').read_text())
        print(f'Finished {ref}, repeat {repeat}, {a.profile}/{a.step}',flush=True)
        return result
    jobs=[(i,ref,r) for i,ref in enumerate(a.refs) for r in range(1,a.repeats+1)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        results=list(pool.map(run,jobs))
    controls={(r['level_version'],r['width'],r['terrain_hash'],r['ram'],r['freq'],r['scale'],r['view'],r['mobs']) for r in results}
    if len(controls)!=1: raise RuntimeError('Mismatched benchmark scenes/profiles; comparison rejected')
    summary={}
    for ref in a.refs:
        runs=[r for r in results if r['ref']==ref]
        summary[ref]={}
        for phase in runs[0]['phases']:
            samples=[r['phases'][phase] for r in runs]
            summary[ref][phase]={key:statistics.median(s[key] for s in samples)
                                 for key in samples[0]}
            summary[ref][phase]['fps_min']=min(s['fps'] for s in samples)
            summary[ref][phase]['fps_max']=max(s['fps'] for s in samples)
    output=ROOT/'build/perf-audit'/f'summary-{a.profile}-{a.step}-w{a.width}-m{a.mobs}.json'
    output.write_text(json.dumps(dict(profile=a.profile,repeats=a.repeats,controls=list(controls)[0],
                                     summary=summary,runs=results),indent=2)+'\n')
    print(json.dumps(summary,indent=2))
    print(output)


if __name__=='__main__': main()
