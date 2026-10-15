"""Build an external PCM bank from retained original-quality, licensed masters.

assets/audio/picks.json names each group's takes, chosen by ear: Kenney masters,
other CC0/CC-BY recordings retained unmodified in assets/audio/extra, or sounds
synthesized here from fixed seeds (procedural, no third-party recording).
"""
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
PROCEDURAL=(('explode',2),('hiss',1),('groan',3))
# Bank group numbers: audio_bank.h A_* order. 'crunch', 'stone', 'wood', 'sand'
# and 'break_gravel' are block break sounds; mining hits reuse the footsteps.
BANK_GROUPS=('crunch','stone','wood','step_grass','step_stone','step_wood','place','hurt','glass','click',
             'sand','explode','hiss','groan','step_sand','step_gravel','break_gravel',
             'pig_say','pig_hurt','pig_death','sheep_say','sheep_hurt','sheep_death',
             'chicken_say','chicken_hurt','chicken_death','zombie_hurt','zombie_death',
             'creeper_hurt','creeper_death','water_in','swim')
# The takes each group plays, chosen by ear in the sound picker: 'kenney/<file>'
# (masters/), 'procedural/<group>_<n>' (synthesized here), or a path under
# extra/ ('#n' picks the nth sound of a recording split at its pauses).
PICKS=ROOT/'assets/audio/picks.json'
EXTRA=ROOT/'assets/audio/extra'
PROCEDURAL_RECIPES={
    'explode':'two-pole noise with a falling low-pass, falling sine boom and decaying crackle; tanh saturation',
    'hiss':'high-passed noise with a slow swell and flutter',
    'groan':'sawtooth glottal source with period-doubling fry and breath noise through three moving vowel formants; tanh rasp',
}
MAGIC=b'DCSFX01\0'
# Every clip's loudest 50 ms reaches LOUDNESS (RMS of full scale) unless its
# peak would pass PEAK first. Event balance is the game's per-effect levels.
LOUDNESS=0.26
PEAK=0.95


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


def one_pole(np,x,cutoff,rate):
    """Low-pass with a per-sample cutoff (Hz)."""
    a=np.exp(-2*np.pi*np.broadcast_to(cutoff,x.shape)/rate);y=np.empty_like(x);s=0.0
    for i in range(len(x)):
        s=a[i]*s+(1-a[i])*x[i];y[i]=s
    return y


def resonator(np,x,freq,width,rate):
    """Two-pole band-pass with per-sample centre frequency (Hz)."""
    r=np.exp(-np.pi*width/rate);b2=-r*r;gain=1-r
    b1=2*r*np.cos(2*np.pi*np.broadcast_to(freq,x.shape)/rate)
    y=np.empty_like(x);y1=y2=0.0
    for i in range(len(x)):
        v=gain*x[i]+b1[i]*y1+b2*y2;y2=y1;y1=v;y[i]=v
    return y


def explosion(np,seed,rate=22050,duration=1.0):
    rng=np.random.default_rng(seed);n=int(rate*duration);t=np.arange(n)/rate
    cutoff=(2600+400*rng.random())*np.exp(-t/0.22)+140
    rumble=one_pole(np,one_pole(np,rng.standard_normal(n),cutoff,rate),cutoff,rate)
    rumble*=np.minimum(1,t/0.004)*np.exp(-t/(0.30+0.06*rng.random()))
    rumble/=np.max(np.abs(rumble))
    sweep=28+(50+10*rng.random())*np.exp(-t/0.45)
    boom=np.sin(2*np.pi*np.cumsum(sweep)/rate)*np.minimum(1,t/0.006)*np.exp(-t/0.28)
    sparks=(rng.random(n)<0.006*np.exp(-t/0.18))*rng.standard_normal(n)
    sparks=one_pole(np,sparks,1800,rate)*np.exp(-t/0.25);sparks/=max(1e-6,np.max(np.abs(sparks)))
    mix=rumble+0.55*boom+0.35*sparks
    return np.tanh(1.8*mix/np.max(np.abs(mix)))


def hiss(np,seed,rate=22050,duration=1.4):
    rng=np.random.default_rng(seed);n=int(rate*duration);t=np.arange(n)/rate
    noise=rng.standard_normal(n);bright=noise-one_pole(np,noise,1400,rate)
    flutter=1+0.12*np.sin(2*np.pi*23*t+rng.random()*6.3)
    envelope=np.minimum(1,t/0.06)*(0.8+0.2*t/duration)*np.minimum(1,(duration-t)/0.12)
    out=bright*flutter*envelope
    return out/np.max(np.abs(out))


def groan(np,seed,rate=44100,duration=1.1):
    rng=np.random.default_rng(seed);duration+=0.25*rng.random()
    n=int(rate*duration);t=np.arange(n)/rate;u=t/duration
    base=86+20*rng.random()
    jitter=one_pole(np,rng.standard_normal(n),12,rate);jitter*=0.015/max(1e-6,np.max(np.abs(jitter)))
    f0=base*(1+0.18*np.sin(np.pi*u*0.9))*(1-0.25*u)*(1+0.02*np.sin(2*np.pi*5.3*t)+jitter)
    phase=np.cumsum(f0)/rate
    saw=2*(phase%1)-1
    fry=0.65+0.35*(np.floor(phase)%2)
    source=saw*fry+0.12*rng.standard_normal(n)
    shift=1+0.08*(rng.random()-0.5)
    formants=((620,420,90),(1150,750,110),(2450,2300,160))
    voice=sum(resonator(np,source,(start+(end-start)*u)*shift,width,rate)*weight
              for (start,end,width),weight in zip(formants,(1.0,0.7,0.35)))
    envelope=np.sin(np.minimum(1,t/0.15)*np.pi/2)**2*np.minimum(1,(duration-t)/0.3)
    envelope*=1+0.1*np.sin(2*np.pi*3.3*t)
    out=voice*envelope
    return np.tanh(2.0*out/np.max(np.abs(out)))


def procedural(np):
    """(group, variant, seed, rate, mono samples) for each synthesized clip."""
    makers={'explode':explosion,'hiss':hiss,'groan':groan}
    rates={'explode':22050,'hiss':22050,'groan':44100}
    for index,(group,count) in enumerate(PROCEDURAL):
        for variant in range(count):
            seed=0x5EED0000+index*16+variant
            yield group,variant,seed,rates[group],makers[group](np,seed)


def split(np,samples,source_rate):
    """A long recording's separate sounds: cut where it stays quiet for 80 ms
    (the sound picker's numbering of '#n' takes)."""
    level=np.abs(samples.mean(axis=1))
    width=int(source_rate*0.01)
    envelope=np.convolve(level,np.ones(width)/width,mode='same')
    loud=envelope>max(0.01,envelope.max()*0.08)
    parts,start,quiet=[],None,0
    for i,value in enumerate(loud):
        if value:
            if start is None:start=i
            quiet=0
        elif start is not None:
            quiet+=1
            if quiet>source_rate*0.08:parts.append((start,i-quiet));start,quiet=None,0
    if start is not None:parts.append((start,len(level)))
    return [samples[max(0,a-int(source_rate*0.01)):b+int(source_rate*0.05)]
            for a,b in parts if b-a>source_rate*0.05]


def build(output,rate=11025,bits=8,channels=1):
    # Host conversion tools only; no decoder or resampler is linked into DOS.
    sys.path.insert(0,str(ROOT/'build/deps/audio-python'))
    import numpy as np
    import soundfile as sf
    manifest=json.loads((MASTERS.parent/'sources.json').read_text())
    records=[];payload=bytearray();rows=[]

    def loudest(samples):
        window=max(1,rate//20);mono=samples.mean(axis=1)
        return max(0.001,max(float(np.sqrt(np.mean(mono[a:a+window]**2)))
                             for a in range(0,max(1,len(mono)-window+1),max(1,window//4))))

    def finish(samples,source_rate,onset):
        """Resample, loudness-normalize, edge-fade; onset is in source frames."""
        if rate<source_rate:
            t=np.arange(-31,32,dtype='float64');cutoff=0.88*rate/source_rate
            kernel=np.sinc(cutoff*t)*np.hamming(len(t));kernel/=kernel.sum()
            samples=np.stack([np.convolve(samples[:,c],kernel,mode='same') for c in range(channels)],axis=1)
        positions=np.arange(max(1,round(len(samples)*rate/source_rate)))*source_rate/rate
        samples=np.stack([np.interp(positions,np.arange(len(samples)),samples[:,c]) for c in range(channels)],axis=1)
        samples*=min(LOUDNESS/loudest(samples),PEAK/max(0.001,float(np.max(np.abs(samples)))))
        # Fade in across the silence before the onset only: a fade over the
        # attack itself dulls and quietens percussive sounds.
        fade=min(len(samples)//2,max(1,rate//300))
        lead=max(1,min(fade,int(onset*rate/source_rate)))
        samples[:lead]*=np.linspace(0,1,lead)[:,None]
        samples[-fade:]*=np.linspace(1,0,fade)[:,None]
        return samples

    def add(group_name,samples,row):
        pcm=(np.clip(np.rint(samples*127)+128,0,255).astype('uint8').tobytes() if bits==8 else
             np.clip(np.rint(samples*32767),-32768,32767).astype('<i2').tobytes())
        records.append(struct.pack('<4I',BANK_GROUPS.index(group_name),len(payload),len(samples),checksum(pcm)))
        payload.extend(pcm);rows.append({**row,'frames':len(samples),'pcm_sha256':sha256(pcm).hexdigest()})

    kenney={row['file']:row for row in manifest['files']}
    extra={row['file']:row for row in json.loads((EXTRA/'sources.json').read_text())['files']}
    synthesized={f'{group}_{variant+1}':(seed,source_rate,mono) for group,variant,seed,source_rate,mono in procedural(np)}

    def recording(source,source_rate,row,part=None):
        samples=source if source.ndim==2 else source[:,None]
        if channels==1:samples=samples.mean(axis=1,keepdims=True)
        elif samples.shape[1]==1:samples=np.repeat(samples,2,axis=1)
        samples=samples[:,:channels];samples-=samples.mean(axis=0,keepdims=True)
        if part is not None:
            pieces=split(np,samples,source_rate)
            if not 1<=part<=len(pieces):raise ValueError(f'{row["take"]}: only {len(pieces)} sounds in the recording')
            samples=pieces[part-1]
        audible=np.flatnonzero(np.max(np.abs(samples),axis=1)>0.006)
        if not len(audible):raise ValueError('Silent source: '+row['take'])
        first=max(0,int(audible[0])-int(source_rate*0.002))
        last=min(int(audible[-1])+1,first+int(source_rate*0.7))
        return finish(samples[first:last],source_rate,int(audible[0])-first)

    picks=json.loads(PICKS.read_text())
    unknown=set(picks)-set(BANK_GROUPS)
    if unknown:raise ValueError('Unknown sound group(s) in picks.json: '+', '.join(sorted(unknown)))
    # A group without picks stays empty: the game then plays its synthesized
    # Towns sound (block breaks, hurt, click) or nothing (mob calls, water).
    for group in BANK_GROUPS:
        for take in picks.get(group,[]):
            kind,_,rest=take.partition('/')
            if kind=='procedural':
                seed,source_rate,mono=synthesized[rest]
                add(group,finish(np.repeat(mono[:,None],channels,axis=1),source_rate,0),
                    dict(group=group,take=take,procedural=PROCEDURAL_RECIPES[rest.rsplit('_',1)[0]],
                         seed=seed,source_rate=source_rate,license='Generated'))
                continue
            path,_,part=take.partition('#')
            meta=kenney.get(rest) if kind=='kenney' else extra.get(path)
            if meta is None:raise ValueError('No retained source for '+take)
            source=((MASTERS/rest) if kind=='kenney' else (EXTRA/path)).read_bytes()
            if sha256(source).hexdigest()!=meta['sha256']:raise ValueError('Source hash mismatch: '+take)
            samples,source_rate=sf.read(BytesIO(source),always_2d=True,dtype='float64')
            row=dict(group=group,take=take,license=meta.get('license',manifest['license']),
                     author=meta.get('author','Kenney'),source_rate=source_rate)
            add(group,recording(samples,source_rate,row,int(part) if part else None),row)
    header=struct.pack('<8s6I',MAGIC,1,rate,bits|(channels<<16),len(records),len(payload),checksum(payload))
    data=header+b''.join(records)+payload
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(data)
    report=dict(format='DCSFX01',rate=rate,bits=bits,channels=channels,bytes=len(data),
                sha256=sha256(data).hexdigest(),source=manifest['source'],license=manifest['license'],
                recipe=('trim silence; cap at 0.7 seconds; 63-tap low-pass; resample; normalize the loudest 50 ms '
                        f'to {LOUDNESS} RMS with a {PEAK} peak ceiling; fade in before the onset only; short fade out'),
                procedural=dict(groups=dict(PROCEDURAL),recipes=PROCEDURAL_RECIPES,
                                note='Synthesized from fixed seeds by this script; no third-party recordings'),
                samples=rows)
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
