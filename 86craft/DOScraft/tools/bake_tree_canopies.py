"""Add sparse leaf-free samples and natural-tree records to an existing seed.

The original panorama map and all terrain/save offsets stay unchanged. No world
generation is repeated, and there is no second full-size surface map.
"""
from pathlib import Path
import argparse
import struct
from bake_far_field import surface
from bake_large_world import checksum
from large_world_media import check_terrain,FAR_BYTES,FAR_MAP_BYTES,TREE_MAGIC


def append_trees(terrain):
    check_terrain(terrain)
    if len(terrain)!=FAR_BYTES:raise ValueError('Expected the existing distant-map seed')
    panorama=terrain[-FAR_MAP_BYTES:];under={};trees=[];columns=[0]
    for cz in range(64):
        for cx in range(64):
            offset=(33+(cz*64+cx)*72)*512;blocks=terrain[offset:offset+12288]
            ground=surface(blocks,leaves=False)
            for z in range(8):
                for x in range(8):
                    index=(cz*8+z)*512+cx*8+x
                    if panorama[index*2+1]==7:
                        under[index]=ground[(z*8+x)*2:(z*8+x+1)*2]
            for z in range(16):
                for x in range(16):
                    p=blocks[(z*16+x)*48:(z*16+x+1)*48];base=p.find(bytes([6]))
                    if base<0:continue
                    top=base
                    while top<48 and p[top]==6:top+=1
                    if 4<=top-base<=6 and top+1<48 and p[top]==p[top+1]==7:
                        trees.append((cx*16+x,cz*16+z,top,base,1,0))
            columns.append(len(trees))
    records=bytearray();groups=bytearray()
    for group in range(8192):
        mask=0;first=len(records)//2
        for bit in range(32):
            index=group*32+bit
            if index in under:mask|=1<<bit;records.extend(under[index])
        groups.extend(struct.pack('<II',mask,first))
    payload=groups+records
    payload.extend(bytes((-len(payload))&3))
    payload.extend(struct.pack('<4097I',*columns))
    for tree in trees:payload.extend(struct.pack('<HHBBBB',*tree))
    payload.extend(bytes((-len(payload))&511))
    result=bytearray(terrain)
    struct.pack_into('<6I',result,92,TREE_MAGIC,FAR_BYTES//512,len(payload),checksum(payload),len(under),len(trees))
    result.extend(payload)
    return result,dict(sparse_cells=len(under),trees=len(trees),metadata_bytes=len(payload),
        panorama_unchanged=result[178291200:FAR_BYTES]==panorama)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--terrain',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    data,report=append_trees(a.terrain.read_bytes())
    with a.output.open('xb') as f:f.write(data)
    print(report)
