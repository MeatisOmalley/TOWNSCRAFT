"""Bake the original shared B8 surface, optionally clipping leaves to 3x3 trunks.

Only the distant appendix changes. Terrain, lighting and V5 save identity remain
unchanged. The renderer consumes the same height/material entries as before.
"""
import argparse
from pathlib import Path
import struct
from bake_large_world import checksum
from large_world_media import check_terrain,LIT_BYTES,FAR_MAP_BYTES,FAR_MAGIC

CUBES=frozenset([*range(1,16),21,22,*range(25,30)])
COMPACT_MAGIC=0x3946414c


def root_heights(blocks):
    """Highest real log in each vertical stack, ignoring unused metadata bits."""
    variants=tuple(bytes([6|(m<<5)]) for m in range(8))
    return bytes(max(blocks[c*48:(c+1)*48].rfind(v) for v in variants)+1 for c in range(256))


def keep_leaf(roots,x,y,z):
    return any(h and h-2<=y<h+3 for dz in range(3) for dx in range(3)
               for h in (roots[(z+dz)*18+x+dx],))


def surface(blocks,leaves=True,roots=None):
    result=bytearray(128)
    for z in range(0,16,2):
        for x in range(0,16,2):
            h,b=0,0
            for dz in range(2):
                for dx in range(2):
                    p=((z+dz)*16+x+dx)*48
                    for y in range(47,h-1,-1):
                        block=blocks[p+y]&31
                        if block not in CUBES:continue
                        if block==7 and (not leaves or roots is not None and not keep_leaf(roots,x+dx,y,z+dz)):continue
                        h,b=y+1,block;break
            at=(z//2*8+x//2)*2
            result[at:at+2]=bytes((h,b))
    return result


def append_map(terrain,*,compact=False):
    check_terrain(terrain)
    if len(terrain)<LIT_BYTES: raise ValueError('Expected a lighting appendix')
    result=bytearray(terrain[:LIT_BYTES]);result[92:116]=bytes(24)
    roots=None
    if compact:
        struct.pack_into('<I',result,92,COMPACT_MAGIC)
        roots=bytearray(1024*1024)
        for cz in range(64):
            for cx in range(64):
                offset=(33+(cz*64+cx)*72)*512
                heights=root_heights(terrain[offset:offset+12288])
                for z in range(16):roots[(cz*16+z)*1024+cx*16:(cz*16+z)*1024+cx*16+16]=heights[z*16:z*16+16]
    summary=bytearray(FAR_MAP_BYTES)
    for cz in range(64):
        for cx in range(64):
            halo=None
            if compact:
                halo=bytearray(18*18)
                for z in range(18):
                    wz=cz*16+z-1
                    if not 0<=wz<1024:continue
                    for x in range(18):
                        wx=cx*16+x-1
                        if 0<=wx<1024:halo[z*18+x]=roots[wz*1024+wx]
            offset=(33+(cz*64+cx)*24*3)*512
            cells=surface(terrain[offset:offset+12288],roots=halo)
            for z in range(8):
                at=((cz*8+z)*512+cx*8)*2
                summary[at:at+16]=cells[z*16:z*16+16]
    struct.pack_into('<5I',result,72,FAR_MAGIC,LIT_BYTES//512,FAR_MAP_BYTES,checksum(summary),1)
    result.extend(summary)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--terrain',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compact-leaves',action='store_true',help='Keep actual leaves within one block of each trunk')
    a=p.parse_args();data=append_map(a.terrain.read_bytes(),compact=a.compact_leaves)
    with a.output.open('xb') as f:f.write(data)
    print(f'{a.output}: {len(data)} bytes; distant map {FAR_MAP_BYTES} bytes')

if __name__=='__main__':main()
