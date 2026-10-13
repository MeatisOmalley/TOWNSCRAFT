"""Compatible optional lighting appendix; base/edit offsets and V5 saves stay fixed."""
import hashlib
import struct
from fat_image import FatImage,populate,scratch_hdd
BASE_BYTES=151011840
LIT_BYTES=178291200
FAR_MAP_BYTES=512*512*2
FAR_BYTES=LIT_BYTES+FAR_MAP_BYTES
FAR_MAGIC=0x31465254
LIGHT_MAGIC=0x314c4354
TREE_MAGIC=0x31525446

def check_terrain(terrain):
    if terrain[:16]!=b'DOSCRAFT-1024-V1' or (len(terrain) not in (BASE_BYTES,LIT_BYTES,FAR_BYTES) and len(terrain)<=FAR_BYTES):
        raise ValueError('Expected a checked prebaked 1024 terrain image')
    if len(terrain)>=LIT_BYTES and struct.unpack_from('<3I',terrain,52)!=(LIGHT_MAGIC,294977,13):
        raise ValueError('Invalid prebaked lighting appendix')
    if len(terrain)>=FAR_BYTES:
        magic,sector,size,total,shift=struct.unpack_from('<5I',terrain,72)
        if (magic,sector,size,shift)!=(FAR_MAGIC,LIT_BYTES//512,FAR_MAP_BYTES,1):
            raise ValueError('Invalid distant terrain appendix')
    if len(terrain)>FAR_BYTES:
        magic,sector,size,total,cells,trees=struct.unpack_from('<6I',terrain,92)
        expected=65536+cells*2;expected=(expected+3)&~3;expected+=4097*4+trees*8
        if (magic,sector)!=(TREE_MAGIC,FAR_BYTES//512) or cells>262144 or trees>1048576 or \
           size!=(expected+511)&~511 or len(terrain)!=FAR_BYTES+size:
            raise ValueError('Invalid natural-tree appendix')

def identity(terrain):
    check_terrain(terrain)
    return hashlib.sha256(terrain[:52]+terrain[512:33*512]).hexdigest()

def upgrade_disk(old_disk,new_terrain):
    """Copy old edit slots and the save file into a newly laid out FAT volume."""
    check_terrain(new_terrain)
    start=struct.unpack_from('<I',old_disk,454)[0]*512
    old=FatImage(old_disk,start)
    if any(attr&16 for name,attr,cluster,size in old.directory()):
        raise ValueError('Cannot migrate an unexpected player disk directory')
    previous=old.read('TERRAIN.TMP')
    if identity(previous)!=identity(new_terrain):
        raise ValueError('Cannot retain player edits across a different baked seed')
    merged=bytearray(new_terrain)
    merged[33*512:BASE_BYTES]=previous[33*512:BASE_BYTES]
    files=[(name,bytes(merged) if name=='TERRAIN.TMP' else old.read(name))
           for name,attr,cluster,size in old.directory() if not attr&16]
    disk=scratch_hdd(cylinders=800,heads=8,sectors=63)
    partition=struct.unpack_from('<I',disk,454)[0]*512
    disk[partition:]=populate(disk[partition:partition+512],files)
    return disk
