"""Boot an uninstrumented ISO, send Return through Tsugaru's keyboard,
and require real gameplay and advancing frames/ticks at the requested RAM.
Uses the matching ELF only to locate symbols; never modifies guest memory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'build/tools/python'))
from elftools.elf.elffile import ELFFile

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory',type=Path,help='Production build containing audit.ISO and kernel.elf')
    ap.add_argument('--ram',type=int,choices=[2,4,6,8],default=2)
    ap.add_argument('--hdd',type=Path,help='Dedicated, already marked terrain scratch image')
    a=ap.parse_args(); out=a.directory.resolve()
    prefix='smoke' if a.ram==2 and not a.hdd else f'smoke-{a.ram}mb-'+('hdd' if a.hdd else 'ram')
    with (out/'kernel.elf').open('rb') as f:
        syms={s.name:s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
    iso=out/'audit.ISO'
    with (out/(prefix+'.log')).open('w') as log:
        args=[str(ROOT/'build/emulator/main_headless/Release/Tsugaru_Headless.exe'),
            str(ROOT/'build/play/STUBROM'),'-CD',str(iso),'-TOWNSTYPE','MODEL2','-FREQ','16',
            '-MEMSIZE',str(a.ram),'-DIFFMOUSE','-DONTAUTOSAVECMOS','-NOWAITBOOT']
        if a.hdd:
            args += ['-TOWNSTYPE','2F','-USEFPU','-CDSPEED','1','-NORMALSCSI','-NORMALFD','-HD0',str(a.hdd.resolve())]
        p=subprocess.Popen(args,
            stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,text=True)
        def command(c): p.stdin.write('!'+c+'\n'); p.stdin.flush()
        def word(data,name): return struct.unpack_from('<I',data,syms[name])[0]
        started=False; first=None; deadline=time.monotonic()+180
        try:
            while time.monotonic()<deadline:
                if p.poll() is not None: raise RuntimeError('Emulator exited before gameplay')
                time.sleep(1)
                dump=out/(prefix+'-ram.bin')
                if dump.exists(): dump.unlink()
                command('SAVEMEMDUMP '+str(dump)+' PHYS:0 100000')
                wait=time.monotonic()+2
                while time.monotonic()<wait and (not dump.exists() or dump.stat().st_size!=0x100000): time.sleep(.05)
                if not dump.exists() or dump.stat().st_size!=0x100000: continue
                data=dump.read_bytes()
                expected={2:96,4:160,6:208,8:256}[a.ram]
                if not started and word(data,'g_W')==expected and word(data,'g_ticks')>20:
                    command('TYPE '); started=True
                if word(data,'state')==1 and word(data,'g_meshQuads')>0:
                    values=[word(data,n) for n in ('g_ticks','fpsFrames','g_meshVersion')]
                    if first is None: first=values
                    elif values[0]>first[0]+100 and values[1]!=first[1]:
                        result=dict(gameplay=True,width=word(data,'g_W'),ticks=values[0],
                                    mesh_quads=word(data,'g_meshQuads'),frames_advancing=True,
                                    iso_sha256=hashlib.sha256(iso.read_bytes()).hexdigest())
                        if a.hdd:
                            assert word(data,'terrainActive')==1, 'HDD terrain backend did not activate'
                            counters=list(struct.unpack_from('<4I',data,syms['g_terrainDiskStats']))
                            assert counters[1]==(expected//16)**2 and counters[0]>0 and counters[3]==0, counters
                            result['terrain_disk_stats']=counters
                        (out/(prefix+'.json')).write_text(json.dumps(result,indent=2)+'\n')
                        command('SS '+str(out/(prefix+'.png')))
                        print(json.dumps(result)); return
            raise RuntimeError('Production image did not reach advancing gameplay')
        finally:
            if p.poll() is None:
                command('Q')
                try: p.wait(timeout=10)
                except subprocess.TimeoutExpired: p.kill(); p.wait()

if __name__=='__main__': main()
