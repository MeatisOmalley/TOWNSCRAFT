"""Bake one leaf-free surface map shared by the B8 band and panorama.

No generation, terrain edits, base checksums or save identity change. Output
is exclusive; the source remains intact. Surface entries are (height, block).
"""
import argparse
from pathlib import Path
import struct
from bake_large_world import checksum
from large_world_media import check_terrain,LIT_BYTES,FAR_MAP_BYTES,FAR_MAGIC

CUBES=frozenset([*range(1,16),21,22,*range(25,30)])
CAP_MAGIC=0x31504143

def surface(blocks,leaves=False,caps=False):
    result=bytearray(128)
    for z in range(0,16,2):
        for x in range(0,16,2):
            h,b,cap=0,0,0
            for dz in range(2):
                for dx in range(2):
                    p=((z+dz)*16+x+dx)*48
                    for y in range(47,h-1,-1):
                        block=blocks[p+y]&31
                        if block in CUBES and (leaves or block!=7):
                            h,b=y+1,block
                            cap=0
                            if caps and block==6 and not blocks[p+y]&64:
                                logs=0
                                for yy in range(y,-1,-1):
                                    if blocks[p+yy]&31!=6 or blocks[p+yy]&64:break
                                    logs+=1
                                if 4<=logs<=6:
                                    while h+cap<48 and cap<3 and blocks[p+h+cap]&31==7:cap+=1
                            break
            at=(z//2*8+x//2)*2
            result[at:at+2]=bytes((h|(cap<<6),b))
    return result

def append_map(terrain,*,caps=False):
    check_terrain(terrain)
    if len(terrain)<LIT_BYTES: raise ValueError('Expected a lighting appendix')
    result=bytearray(terrain[:LIT_BYTES])
    # Drop the checkpointed canopy appendix when rebaking an existing map.
    result[92:116]=bytes(24)
    if caps:struct.pack_into('<I',result,92,CAP_MAGIC)
    summary=bytearray(FAR_MAP_BYTES)
    for cz in range(64):
        for cx in range(64):
            offset=(33+(cz*64+cx)*24*3)*512
            cells=surface(terrain[offset:offset+12288],caps=caps)
            for z in range(8):
                at=((cz*8+z)*512+cx*8)*2
                summary[at:at+16]=cells[z*16:z*16+16]
    struct.pack_into('<5I',result,72,FAR_MAGIC,LIT_BYTES//512,FAR_MAP_BYTES,checksum(summary),1)
    result.extend(summary)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--terrain',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--caps',action='store_true',help='Natural green caps encoded in the same height bytes')
    a=p.parse_args()
    data=append_map(a.terrain.read_bytes(),caps=a.caps)
    with a.output.open('xb') as f: f.write(data)
    print(f'{a.output}: {len(data)} bytes; distant map {FAR_MAP_BYTES} bytes')

if __name__=='__main__': main()
