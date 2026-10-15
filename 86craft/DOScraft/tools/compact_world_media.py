"""Save-preserving V1 -> V2 RLE conversion, including both committed banks."""
import struct
from bake_large_world import checksum
from convert_large_v2 import convert,check_v2,CELLS,DEFAULT_POOL_PAIRS
from large_world_media import check_terrain,FAR_BYTES
from leaf_flags_media import BANK,DIRECTORY,DIRECTORY_END,SAVE_VERSIONS,decode,encode,save_sum,validate_save_preservation


def grow_edit_pool(terrain,pool_pairs):
    """Append empty pairs without moving any existing base, edit or save slot."""
    check_terrain(terrain)
    if terrain[:16]!=b'DOSCRAFT-1024-V2':raise ValueError('Expected V2 terrain')
    pool,pairs,total=struct.unpack_from('<3I',terrain,132)
    if not pool or not pairs or total!=pool+pairs*48:raise ValueError('Invalid V2 edit pool extent')
    if not pairs<=pool_pairs<=4096:raise ValueError('Cannot shrink or oversize the edit pool')
    if pool_pairs==pairs:return bytes(terrain)
    result=bytearray(terrain)
    result.extend(bytes((pool_pairs-pairs)*48*512))
    struct.pack_into('<2I',result,136,pool_pairs,pool+pool_pairs*48)
    check_v2(result)
    return bytes(result)


def convert_player(previous,save,pool_pairs=DEFAULT_POOL_PAIRS):
    check_terrain(previous)
    if previous[:16]!=b'DOSCRAFT-1024-V1' or len(previous)!=FAR_BYTES or len(save)!=BANK*2:
        raise ValueError('Expected V1 distant terrain and a complete V5 save medium')
    if checksum(previous[512:33*512])!=struct.unpack_from('<I',previous,48)[0]:
        raise ValueError('Corrupt V1 base index')
    fresh=bytearray(previous);references={};banks=[]
    for c in range(4096):
        at=(33+c*72)*512
        if checksum(previous[at:at+CELLS])!=struct.unpack_from('<I',previous,512+c*4)[0]:
            raise ValueError('Corrupt V1 base column '+str(c))
        fresh[at+CELLS:at+3*CELLS]=bytes(2*CELLS)
    for bank in range(2):
        start=bank*BANK
        magic,version,total,length,generation=struct.unpack_from('<5I',save,start)
        if magic!=0x46435354:continue
        # Version 8 adds per-block edits, which V1/V2 media never hold.
        if version not in SAVE_VERSIONS or (version>=8 and struct.unpack_from('<I',save,start+DIRECTORY_END)[0]) or not DIRECTORY_END-20<=length<=BANK-20 or save_sum(save[start+20:start+20+length])!=total:
            raise ValueError('Invalid committed V1 save bank')
        seed,index,count=struct.unpack_from('<3I',save,start+DIRECTORY)
        if (seed,index,count)!=(struct.unpack_from('<I',previous,36)[0],struct.unpack_from('<I',previous,48)[0],4096):
            raise ValueError('Saved world does not match V1 terrain')
        banks.append((start,length))
        for c in range(4096):
            slot,n,total,raw=struct.unpack_from('<4I',save,start+DIRECTORY+12+c*16)
            if slot>2 or not 0<n<=CELLS or raw>1 or (raw and n!=CELLS):
                raise ValueError('Invalid saved V1 terrain record')
            if not slot:
                if (n,total,raw)!=(CELLS,struct.unpack_from('<I',previous,512+c*4)[0],1):
                    raise ValueError('Invalid saved V1 base record')
            else:
                key=(c,slot);record=(n,total,raw)
                if key in references and references[key]!=record:
                    raise ValueError('Save banks disagree about a V1 edit slot')
                references[key]=record
    columns=sorted({c for c,slot in references})
    pairs=max(pool_pairs,len(columns))
    if not 1<=pairs<=4096:raise ValueError('Invalid V2 edit pool size')
    pair_for={c:pair for pair,c in enumerate(columns)}
    result=bytearray(convert(fresh,pool_pairs=pairs))
    check_v2(result,fresh)
    pool=struct.unpack_from('<I',result,132)[0];converted={}
    for (c,slot),(n,total,raw) in references.items():
        old=(33+c*72+slot*24)*512;payload=previous[old:old+n]
        if checksum(payload)!=total:raise ValueError('Corrupt saved V1 edit slot')
        blocks=decode(payload,raw);packed,new_raw=encode(blocks)
        at=(pool+(pair_for[c]*2+slot-1)*24)*512
        result[at:at+len(packed)]=packed
        # Independent roundtrip: no logical block or metadata changes.
        if decode(result[at:at+len(packed)],new_raw)!=blocks:
            raise ValueError('Edited column RLE roundtrip failed')
        converted[c,slot]=(slot|(pair_for[c]<<2),len(packed),checksum(packed),new_raw)
    sums=b''.join(result[512+c*12+8:512+c*12+12] for c in range(4096))
    new_index_sum=checksum(sums);updated=bytearray(save)
    for start,length in banks:
        struct.pack_into('<I',updated,start+DIRECTORY+4,new_index_sum)
        for c in range(4096):
            pos=start+DIRECTORY+12+c*16;slot=struct.unpack_from('<I',save,pos)[0]
            if slot:record=converted[c,slot]
            else:
                off,n,total=struct.unpack_from('<3I',result,512+c*12)
                record=(0,n,total,int(n==CELLS))
            struct.pack_into('<4I',updated,pos,*record)
        struct.pack_into('<I',updated,start+8,save_sum(updated[start+20:start+20+length]))
    validate_save_preservation(save,updated,v2_slots=True)
    check_v2(result)
    return bytes(result),bytes(updated),dict(edited_columns=len(columns),edited_snapshots=len(references),
        save_banks=len(banks),pool_pairs=pairs,previous_bytes=len(previous),compact_bytes=len(result))
