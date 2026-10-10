"""Symbolize guest IRQ0 drawing samples, not host timings or FPS evidence."""
import argparse
from bisect import bisect_right
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
from build_imported import COMPILER


def symbols_from_nm(text):
    symbols=[]
    for line in text.splitlines():
        row=line.split()
        if len(row)==3 and row[1] in ('t','T'):
            symbols.append((int(row[0],16),row[2]))
    symbols.sort()
    if not symbols: raise ValueError('No text symbols in the matching executable')
    return symbols


def symbolize(samples,symbols):
    addresses=[row[0] for row in symbols]
    counts=Counter()
    for sample in samples:
        if not 0<sample<2**32: raise ValueError('Invalid guest EIP')
        index=bisect_right(addresses,sample)-1
        counts[symbols[index][1] if index>=0 else '<before-text>']+=1
    total=sum(counts.values())
    return [{'symbol':name,'samples':n,'sample_percent':100*n/total}
        for name,n in counts.most_common()]


def analyze(report,symbols):
    results=[]
    for run in report['results']:
        header=run['header']
        if header.get('result')!='PASS' or not header.get('draw_profiling'):
            raise ValueError('Requires a completed guest draw-sampling diagnostic')
        samples=[int(x,16) for x in header['draw_eip'].split(',') if x]
        if not samples: raise ValueError('No guest code samples captured')
        scenes=[]
        previous=0
        for scene in run['scenes']:
            if 'draw_sample_begin' not in scene: continue  # early aggregate-only checkpoint
            start=int(scene['draw_sample_begin']); end=int(scene['draw_sample_end'])
            if start!=previous or not start<=end<=len(samples):
                raise ValueError('Invalid per-scene sampling ranges')
            previous=end
            scenes.append({'scene':scene['name'],'sample_count':end-start,
                'functions':symbolize(samples[start:end],symbols)})
        if scenes and previous!=len(samples): raise ValueError('Unassigned guest samples')
        results.append({'sample_count':len(samples),'functions':symbolize(samples,symbols),'scenes':scenes})
    return results


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    report=json.loads(args.report.read_text())
    if hashlib.sha256(args.exe.read_bytes()).hexdigest()!=report['build']['exe_sha256']:
        raise ValueError('Executable does not match the guest diagnostic')
    nm=COMPILER.with_name(COMPILER.name.replace('gcc','nm'))
    result=subprocess.run([str(nm),'-n',str(args.exe)],check=True,capture_output=True,text=True)
    data={'note':'100Hz guest IRQ0 EIP samples during drawing, including warm frames. '
        'Illustrative function attribution, not precise instruction timing or performance proof.',
        'exe_sha256':report['build']['exe_sha256'],'results':analyze(report,symbols_from_nm(result.stdout))}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(data,indent=2)+'\n')
    for i,run in enumerate(data['results'],1):
        print('REPEAT',i,'SAMPLES',run['sample_count'])
        for row in run['functions'][:12]:
            print(f'{row["sample_percent"]:5.1f}% {row["samples"]:4d} {row["symbol"]}')


if __name__=='__main__': main()
