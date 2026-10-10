"""Read-only guest evidence extraction; outputs go only to a NEW host directory.

The independent v3 parser checks the actual generated world's payload checksum.
PNG pixels/palette are diagnostic framebuffer dumps, not edited game assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from run_vm import diagnostic_volume


def parse_v3(save):
    if len(save)!=154*8192 or struct.unpack_from('<II',save)!=(0x46435354,3):
        raise ValueError('Expected original full-size v3 raw save')
    checksum=struct.unpack_from('<I',save,8)[0]
    state=struct.unpack_from('<16i',save,12)
    width=state[0]
    if width!=256: raise ValueError('Expected width256 game smoke')
    at=12+64+72; cells=width*width*48; done=0; runs=0
    while done<cells:
        if at+2>len(save): raise ValueError('Truncated terrain')
        run=save[at]; at+=2; done+=run; runs+=1
        if not run or done>cells: raise ValueError('Invalid terrain RLE')
    chests=save[at]; at+=1+chests*57
    if chests>64 or at+12+256*4+8>len(save): raise ValueError('Invalid chest/mob extent')
    region_seed,next_id,regions=struct.unpack_from('<III',save,at); at+=12
    if regions!=256 or not next_id: raise ValueError('Invalid mob region header')
    visited=struct.unpack_from('<256I',save,at); at+=1024
    if any(v>1 for v in visited): raise ValueError('Invalid mob region state')
    active,dormant=struct.unpack_from('<II',save,at); at+=8
    if active>24 or dormant>128: raise ValueError('Invalid mob counts')
    at+=(active+dormant)*21*4
    if at>len(save): raise ValueError('Truncated mob records')
    actual=0
    for byte in save[12:at]: actual=(actual*31+byte)&0xffffffff
    if checksum!=actual: raise ValueError('Save checksum mismatch')
    return {'version':3,'width':width,'player_xyz':list(state[1:4]),
        'yaw_pitch':list(state[4:6]),'health':state[6],'selected':state[7],
        'rle_runs':runs,'chests':chests,'active_mobs':active,'dormant_mobs':dormant,
        'region_seed':region_seed,'payload_end':at,'checksum':checksum,
        'sha256':hashlib.sha256(save).hexdigest()}


def frame_png(raw):
    if len(raw)!=768+320*240: raise ValueError('Invalid framebuffer snapshot')
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    scan=b''.join(b'\0'+raw[768+y*320:768+(y+1)*320] for y in range(240))
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',320,240,8,3,0,0,0))+
        chunk(b'PLTE',raw[:768])+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vm',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    volume=diagnostic_volume((args.vm/'scratch.img').read_bytes())
    reports={name:volume.read(name).decode('ascii') for name in ('PLAYNEW.TXT','PLAYLOAD.TXT')}
    if any('RESULT=PASS\n' not in value or 'RESULT=FAIL\n' in value for value in reports.values()):
        raise ValueError('Both real-game phases must pass')
    save=parse_v3(volume.read('WORLD.SAV'))
    args.output.mkdir(parents=True,exist_ok=False)
    for name in ('TITLE','WORLD','DOWN','PLACE','BREAK','INV','CRAFT'):
        (args.output/(name+'.png')).write_bytes(frame_png(volume.read(name+'.RAW')))
    record={'vm_directory':str(args.vm.resolve()),'build':json.loads((args.vm/'build.json').read_text()),
        'reports':reports,'save':save,'limitations':['Silent audio',
        'AT-controller diagnostic input, not proof of Windows ingress',
        'Snapshots after reload replace new-world images',
        'No comprehensive feature parity or performance acceptance claim',
        'Floppy extracted from ISO; no native optical boot claim']}
    (args.output/'report.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))


if __name__=='__main__': main()
