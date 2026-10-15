"""Append checked skylight without changing the existing terrain/edit offsets."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
from build_imported import ROOT, VENDOR
from bake_large_world import checksum, IMAGE_BYTES

LIGHT_MAGIC=0x314c4354
LIGHT_BASE=IMAGE_BYTES//512+32
LIGHT_BYTES=(LIGHT_BASE+4096*13)*512
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'

def bake_light(source,output):
    terrain=bytearray(Path(source).read_bytes())
    if len(terrain)!=IMAGE_BYTES or terrain[:16]!=b'DOSCRAFT-1024-V1':
        raise ValueError('Expected the original checked 1024 terrain image')
    world=(VENDOR/'src/world.c').read_text().replace('\r\n','\n')
    selected=world[world.index('const u8 g_lightOpacity'):world.index('static void init_hides_tab')]
    selected+=world[world.index('static inline void pq_push'):world.index('static inline void rq_push')]
    selected+=world[world.index('static inline int lget'):world.index('static void remove_light')]
    selected+=world[world.index('static void update_height'):world.index('int world_light_at')]
    with tempfile.TemporaryDirectory(prefix='doscraft-light-bake-') as directory:
        folder=Path(directory)
        (folder/'world_light_reference.inc').write_text(selected)
        exe=folder/'bake.exe'; records=folder/'light.bin'
        subprocess.run([str(ZIG),'cc','-target','x86-windows-gnu','-O2','-std=gnu99',
            '-I',str(folder),'-I',str(VENDOR/'src'),str(ROOT/'tests/bake_light_native.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(Path(source).resolve()),str(records)],check=True)
        light=records.read_bytes()
    if len(light)!=32*512+4096*13*512: raise ValueError('Baked light extent mismatch')
    struct.pack_into('<5I',terrain,52,LIGHT_MAGIC,LIGHT_BASE,13,checksum(light[:32*512]),6400)
    with Path(output).open('xb') as file:
        file.write(terrain); file.write(light)
    print(f'TERRAIN={output}\nBYTES={LIGHT_BYTES}\nBASE_OFFSETS=UNCHANGED',flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--terrain',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args(); bake_light(args.terrain,args.output)

if __name__=='__main__': main()
