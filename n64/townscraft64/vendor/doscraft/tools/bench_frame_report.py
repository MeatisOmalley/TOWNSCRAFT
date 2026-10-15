"""Extract/diff completed diagnostic frame dumps, never timing-gain evidence."""
import argparse
import json
from pathlib import Path
from benchmark import parse
from build_game import ROOT
from game_report import frame_png
from run_vm import diagnostic_volume


def pixel_hash(pixels):
    value=2166136261
    for byte in pixels: value=((value^byte)*16777619)&0xffffffff
    return f'{value:08x}'


def difference(old,new):
    if len(old)!=768+320*240 or len(new)!=len(old):
        raise ValueError('Expected complete palette/indexed 320x240 dumps')
    changed=[i for i,(a,b) in enumerate(zip(old[768:],new[768:])) if a!=b]
    return {'changed_pixels':len(changed),
        'changed_view_pixels':sum(i<320*200 for i in changed),
        'changed_hud_pixels':sum(i>=320*200 for i in changed),
        'palette_bytes_changed':sum(a!=b for a,b in zip(old[:768],new[:768])),
        'bounds':None if not changed else [min(i%320 for i in changed),min(i//320 for i in changed),
            max(i%320 for i in changed),max(i//320 for i in changed)],
        'first_changed_pixels':[{'x':i%320,'y':i//320,'baseline':old[768+i],'candidate':new[768+i]}
            for i in changed[:16]]}


def frames(vm,repeat):
    vm=vm.resolve(strict=True)
    if not vm.is_relative_to(ROOT/'runtime'): raise ValueError('Expected private diagnostic VM')
    volume=diagnostic_volume((vm/'scratch.img').read_bytes())
    if volume.read(f'BENCH{repeat}.END')!=b'PASS\n': raise ValueError('Need completed successful repeat')
    report=parse(volume.read(f'BENCH{repeat}.TXT').decode('ascii'))
    if report['header'].get('frame_dumps')!='TIMINGS_DIAGNOSTIC_ONLY':
        raise ValueError('Need explicit dump diagnostic, not a timed replay')
    images=[volume.read(f'B{repeat}S{i}.RAW') for i in range(6)]
    for image,scene in zip(images,report['scenes']):
        if len(image)!=768+320*240 or pixel_hash(image[768:])!=scene['hash']:
            raise ValueError('Dump differs from captured frame hash')
    return report,images


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-vm',type=Path,required=True)
    parser.add_argument('--candidate-vm',type=Path,required=True)
    parser.add_argument('--repeat',type=int,choices=(1,2,3),default=1)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    old,a=frames(args.baseline_vm,args.repeat); new,b=frames(args.candidate_vm,args.repeat)
    for key in ('seed','scale','view','flat','subdiv','occlusion','samples_per_scene','motion_policy'):
        if old['header'].get(key)!=new['header'].get(key): raise ValueError('Different replay: '+key)
    rows=[]
    for i,(before,after) in enumerate(zip(a,b)):
        name=old['scenes'][i]['name']
        if name!=new['scenes'][i]['name']: raise ValueError('Different scene order')
        rows.append({'scene':name,**difference(before,after)})
    args.output.mkdir(parents=True,exist_ok=False)
    for i,(before,after) in enumerate(zip(a,b)):
        (args.output/f'{i}-baseline.png').write_bytes(frame_png(before))
        (args.output/f'{i}-candidate.png').write_bytes(frame_png(after))
    result={'baseline_vm':str(args.baseline_vm.resolve()),'candidate_vm':str(args.candidate_vm.resolve()),
        'repeat':args.repeat,'note':'Disk dumps are diagnostic only; no gain or complete-gameplay proof.',
        'scenes':rows}
    (args.output/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(rows,indent=2))


if __name__=='__main__': main()
