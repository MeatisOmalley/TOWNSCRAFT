"""Build an external PCM bank from retained original-quality, licensed masters."""
import argparse
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
import struct
import sys
from zipfile import ZipFile

ROOT=Path(__file__).resolve().parents[1]
MASTERS=ROOT/'assets/audio/masters'
GROUPS=(('crunch','impactSoft_medium',4),('stone','impactMining',4),
        ('wood','impactWood_medium',4),('step_grass','footstep_grass',5),
        ('step_stone','footstep_concrete',5),('step_wood','footstep_wood',5),
        ('place','impactPlank_medium',4),('hurt','impactPunch_medium',3),
        ('glass','impactGlass_light',3),('click','impactGeneric_light',3),
        ('sand','footstep_snow',4))
MAGIC=b'DCSFX01\0'


def checksum(data):
    h=2166136261
    for b in data:h=((h^b)*16777619)&0xffffffff
    return h


def import_archive(path):
    data=Path(path).read_bytes();z=ZipFile(BytesIO(data));MASTERS.mkdir(parents=True,exist_ok=True)
    rows=[]
    for group,prefix,count in GROUPS:
        for variant in range(count):
            name=f'{prefix}_{variant:03}.ogg';payload=z.read('Audio/'+name)
            target=MASTERS/name
            if target.exists() and target.read_bytes()!=payload:raise ValueError('Master differs: '+name)
            target.write_bytes(payload)
            rows.append(dict(group=group,variant=variant,file=name,sha256=sha256(payload).hexdigest()))
    license=z.read('License.txt')
    (MASTERS.parent/'KENNEY-LICENSE.txt').write_bytes(license)
    manifest=dict(source='https://kenney.nl/assets/impact-sounds',license='CC0-1.0',
                  archive_sha256=sha256(data).hexdigest(),masters='unmodified original OGG files',files=rows)
    (MASTERS.parent/'sources.json').write_text(json.dumps(manifest,indent=2)+'\n')


def build(output,rate=11025,bits=8,channels=1):
    # Host conversion tools only; no decoder or resampler is linked into DOS.
    sys.path.insert(0,str(ROOT/'build/deps/audio-python'))
    import numpy as np
    import soundfile as sf
    manifest=json.loads((MASTERS.parent/'sources.json').read_text())
    records=[];payload=bytearray();rows=[]
    for row in manifest['files']:
        source=(MASTERS/row['file']).read_bytes()
        if sha256(source).hexdigest()!=row['sha256']:raise ValueError('Master hash mismatch')
        samples,source_rate=sf.read(BytesIO(source),always_2d=True,dtype='float64')
        if channels==1:samples=samples.mean(axis=1,keepdims=True)
        elif samples.shape[1]==1:samples=np.repeat(samples,2,axis=1)
        samples=samples[:,:channels];samples-=samples.mean(axis=0,keepdims=True)
        audible=np.flatnonzero(np.max(np.abs(samples),axis=1)>0.006)
        if not len(audible):raise ValueError('Silent source')
        first=max(0,int(audible[0])-int(source_rate*0.002))
        last=min(int(audible[-1])+1,first+int(source_rate*0.7))
        samples=samples[first:last]
        if rate<source_rate:
            t=np.arange(-31,32,dtype='float64');cutoff=0.88*rate/source_rate
            kernel=np.sinc(cutoff*t)*np.hamming(len(t));kernel/=kernel.sum()
            samples=np.stack([np.convolve(samples[:,c],kernel,mode='same') for c in range(channels)],axis=1)
        positions=np.arange(max(1,round(len(samples)*rate/source_rate)))*source_rate/rate
        samples=np.stack([np.interp(positions,np.arange(len(samples)),samples[:,c]) for c in range(channels)],axis=1)
        samples*=0.75/max(0.001,float(np.max(np.abs(samples))))
        fade=min(len(samples)//2,max(1,rate//300))
        samples[:fade]*=np.linspace(0,1,fade)[:,None]
        samples[-fade:]*=np.linspace(1,0,fade)[:,None]
        pcm=(np.clip(np.rint(samples*127)+128,0,255).astype('uint8').tobytes() if bits==8 else
             np.clip(np.rint(samples*32767),-32768,32767).astype('<i2').tobytes())
        group=next(i for i,g in enumerate(GROUPS) if g[0]==row['group'])
        records.append(struct.pack('<4I',group,len(payload),len(samples),checksum(pcm)))
        payload.extend(pcm);rows.append({**row,'source_rate':source_rate,'frames':len(samples),'pcm_sha256':sha256(pcm).hexdigest()})
    header=struct.pack('<8s6I',MAGIC,1,rate,bits|(channels<<16),len(records),len(payload),checksum(payload))
    data=header+b''.join(records)+payload
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(data)
    report=dict(format='DCSFX01',rate=rate,bits=bits,channels=channels,bytes=len(data),
                sha256=sha256(data).hexdigest(),source=manifest['source'],license=manifest['license'],
                recipe='trim silence; cap at 0.7 seconds; 63-tap low-pass; resample; normalize to 0.75 peak; short edge fades',samples=rows)
    output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--import-archive',type=Path)
    p.add_argument('--output',type=Path,default=ROOT/'build/assets/audio/SOUND.PAK')
    p.add_argument('--rate',type=int,choices=(11025,22050,44100),default=11025)
    p.add_argument('--bits',type=int,choices=(8,16),default=8)
    p.add_argument('--channels',type=int,choices=(1,2),default=1)
    a=p.parse_args()
    if a.import_archive:import_archive(a.import_archive)
    r=build(a.output,a.rate,a.bits,a.channels)
    print(f'{a.output}: {len(r["samples"])} samples, {r["bytes"]} bytes, {a.rate} Hz/{a.bits}-bit/{a.channels} channel(s)')


if __name__=='__main__':main()
