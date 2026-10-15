"""Compatible optional lighting appendix; base/edit offsets and V5 saves stay fixed."""
import hashlib
import struct
from fat_image import FatImage,populate
BASE_BYTES=151011840
LIT_BYTES=178291200
FAR_MAP_BYTES=512*512*2
FAR_BYTES=LIT_BYTES+FAR_MAP_BYTES
FAR_MAGIC=0x31465254
LIGHT_MAGIC=0x314c4354
TREE_MAGIC=0x31525446

V3_MAGIC=b'DOSCRAFT-1024-V3'

def check_terrain(terrain):
    if terrain[:16]==V3_MAGIC:
        from convert_large_v3 import check_v3
        check_v3(terrain); return
    if terrain[:16]==b'DOSCRAFT-1024-V2':
        from convert_large_v2 import check_v2
        check_v2(terrain); return
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
           size!=((expected+511)&~511) or len(terrain)!=FAR_BYTES+size:
            raise ValueError('Invalid natural-tree appendix')

def identity(terrain):
    check_terrain(terrain)
    index_end=97*512 if terrain[:16] in (b'DOSCRAFT-1024-V2',V3_MAGIC) else 33*512
    return hashlib.sha256(terrain[:52]+terrain[512:index_end]).hexdigest()


def log_terrain(terrain,log_sectors=None):
    """Fresh player/diagnostic media: the V3 layout with an empty edit log."""
    from convert_large_v3 import from_v2,DEFAULT_LOG_BYTES
    if terrain[:16]==V3_MAGIC:
        check_terrain(terrain);return bytes(terrain)
    return from_v2(compact_terrain(terrain),log_sectors or DEFAULT_LOG_BYTES//512,checked=True)

def compact_terrain(terrain):
    """The shared V2 RLE layout (the immutable part of V3 media)."""
    if terrain[:16]==b'DOSCRAFT-1024-V2':
        check_terrain(terrain);return bytes(terrain)
    from convert_large_v2 import convert,check_v2
    result=convert(terrain);check_v2(result,terrain)
    return result

def upgrade_disk(old_disk,new_terrain,*,compact=False):
    """Copy old edit slots and the save file into a newly laid out FAT volume."""
    check_terrain(new_terrain)
    start=struct.unpack_from('<I',old_disk,454)[0]*512
    old=FatImage(old_disk,start)
    if any(attr&16 for name,attr,cluster,size in old.directory()):
        raise ValueError('Cannot migrate an unexpected player disk directory')
    previous=old.read('TERRAIN.TMP')
    # Compact publishing moves V2 players onto the edit log as well.
    if previous[:16]==V3_MAGIC or new_terrain[:16]==V3_MAGIC or \
            (compact and previous[:16]==b'DOSCRAFT-1024-V2'):
        return upgrade_log_disk(old_disk,previous,old.read('WORLD.SAV'),new_terrain)
    if previous[:16]==b'DOSCRAFT-1024-V2' or new_terrain[:16]==b'DOSCRAFT-1024-V2':
        from compact_world_media import convert_player
        from leaf_flags_media import upgrade
        save=old.read('WORLD.SAV')
        if previous[:16]!=b'DOSCRAFT-1024-V2':
            previous,save,proof=convert_player(previous,save)
        next_terrain=compact_terrain(new_terrain)
        if identity(previous)==identity(next_terrain):
            before,pairs=struct.unpack_from('<2I',previous,132)
            after,new_pairs=struct.unpack_from('<2I',next_terrain,132)
            if new_pairs<pairs:raise ValueError('New medium would shrink the edit pool')
            merged=bytearray(next_terrain)
            merged[after*512:(after+pairs*48)*512]=previous[before*512:]
        else:
            merged,save=upgrade(previous,next_terrain,save)
        return replace_world_files(old_disk,merged,save)
    migrated_save=None
    if identity(previous)!=identity(new_terrain):
        from leaf_flags_media import upgrade,MAGIC
        if struct.unpack_from('<I',new_terrain,92)[0]!=MAGIC:
            raise ValueError('Cannot retain player edits across a different baked seed')
        merged,migrated_save=upgrade(previous,new_terrain,old.read('WORLD.SAV'))
    else:
        merged=bytearray(new_terrain)
        merged[33*512:BASE_BYTES]=previous[33*512:BASE_BYTES]
    save=migrated_save if migrated_save is not None else old.read('WORLD.SAV')
    if compact:
        from compact_world_media import convert_player
        merged,save,proof=convert_player(bytes(merged),save)
    return replace_world_files(old_disk,merged,save)


def disk_log_sectors(disk,prefix_sectors):
    """The log a V2 player migrates into: 24 MiB, or as much as the disk's FAT
    volume holds beside its other files (the 386's 40 MB disk), after
    prefix_sectors."""
    from convert_large_v3 import LEGACY_LOG_BYTES,log_size_for_volume
    volume=FatImage(disk,struct.unpack_from('<I',disk,454)[0]*512)
    others=[size for name,attr,cluster,size in volume.directory() if name!='TERRAIN.TMP']
    return min(LEGACY_LOG_BYTES//512,log_size_for_volume(volume,prefix_sectors,others))

def upgrade_log_disk(old_disk,previous,save,new_terrain):
    """V2 or V3 player media onto V3 media of the same immutable world.

    A V2 disk migrates both banks and every edit into the log. A V3 disk keeps
    its log byte for byte; its offsets are absolute, so the immutable part must
    be the same, and the log keeps its size. Leaf or seed changes are refused."""
    from convert_large_v3 import migrate_player,log_extent,v3_records,from_v2
    check_terrain(previous)
    if previous[:16] not in (b'DOSCRAFT-1024-V2',V3_MAGIC):
        raise ValueError('Migrate V1 player media to V2 before the edit log')
    first=struct.unpack_from('<I',previous,132)[0]
    count=log_extent(previous)[1] if previous[:16]==V3_MAGIC else disk_log_sectors(old_disk,first)
    target=from_v2(compact_terrain(new_terrain),count,checked=True) if new_terrain[:16]!=V3_MAGIC else new_terrain
    check_terrain(target)
    if (log_extent(target)!=(first,count) or previous[20:132]!=target[20:132] or
            previous[144:first*512]!=target[144:first*512]):
        raise ValueError('Edit-log upgrade requires the same immutable world and log extent')
    if previous[:16]==b'DOSCRAFT-1024-V2':
        merged,save,proof=migrate_player(previous,save,count)
        return replace_world_files(old_disk,merged,save)
    v3_records(previous,save,checked=True)
    merged=bytearray(target)
    merged[first*512:]=previous[first*512:]
    return replace_world_files(old_disk,bytes(merged),save)


def replace_world_files(old_disk,terrain,save):
    """Keep geometry and other root files while replacing checked world data."""
    part=struct.unpack_from('<I',old_disk,454)[0]*512
    old=FatImage(old_disk,part)
    if any(attr&16 for name,attr,cluster,size in old.directory()):
        raise ValueError('Unexpected player disk directory')
    files=[(name,bytes(terrain) if name=='TERRAIN.TMP' else bytes(save)
            if name=='WORLD.SAV' else old.read(name))
           for name,attr,cluster,size in old.directory()]
    disk=bytearray(old_disk)
    disk[part:]=populate(old_disk[part:part+512],files)
    if len(disk)!=len(old_disk):raise ValueError('Disk geometry changed')
    checked=FatImage(disk,part)
    for name,data in files:
        if checked.read(name)!=data:raise ValueError('Player file roundtrip failed')
    return disk
