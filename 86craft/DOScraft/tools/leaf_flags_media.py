"""Offline visibility-metadata migration; preserve all logical blocks and saves."""
import struct
from bake_large_world import checksum

MAGIC=0x3246414c
RIM=64
CELLS=12288
NORMALIZE=bytes(b&~RIM if b&63==7 else b for b in range(256))
BANK=77*8192
DIRECTORY=20+64+72
DIRECTORY_END=DIRECTORY+12+4096*16

def offset(column,slot=0):return (33+column*72+slot*24)*512

def decode(data,raw):
    if raw:
        if len(data)!=CELLS:raise ValueError('Invalid raw leaf column')
        return bytearray(data)
    if len(data)&1:raise ValueError('Invalid compressed leaf column')
    result=bytearray()
    for i in range(0,len(data),2):
        if not data[i]:raise ValueError('Empty leaf column run')
        result.extend(bytes([data[i+1]])*data[i])
    if len(result)!=CELLS:raise ValueError('Invalid decoded leaf column')
    return result

def encode(data):
    runs=bytearray();last=data[0];count=0
    for b in data:
        if b!=last or count==255:runs.extend((count,last));last=b;count=0
        count+=1
    runs.extend((count,last))
    return (bytes(data),1) if len(runs)>=CELLS else (bytes(runs),0)

def save_sum(data):
    value=0
    for b in data:value=(value*31+b)&0xffffffff
    return value

def upgrade(previous,terrain,save):
    if b'DOSCRAFT-1024-V3' in (previous[:16],terrain[:16]):
        raise ValueError('Leaf migration of V3 edit-log media is not implemented')
    if previous[:16]==b'DOSCRAFT-1024-V2':
        return upgrade_v2(previous,terrain,save)
    if previous[:48]!=terrain[:48] or struct.unpack_from('<I',terrain,92)[0]!=MAGIC:
        raise ValueError('Leaf migration requires the same seed and geometry')
    merged=bytearray(terrain)
    for c in range(4096):
        a=previous[offset(c):offset(c)+CELLS];b=terrain[offset(c):offset(c)+CELLS]
        if a.translate(NORMALIZE)!=b.translate(NORMALIZE):
            raise ValueError('Leaf migration would change terrain in column '+str(c))
        if checksum(a)!=struct.unpack_from('<I',previous,512+c*4)[0] or \
           checksum(b)!=struct.unpack_from('<I',terrain,512+c*4)[0]:
            raise ValueError('Invalid immutable column checksum')
        merged[offset(c,1):offset(c,1)+2*CELLS]=previous[offset(c,1):offset(c,1)+2*CELLS]
    updated=bytearray(save);converted={}
    for bank in range(2):
        start=bank*BANK
        magic,version,total,length,generation=struct.unpack_from('<5I',save,start)
        if magic!=0x46435354:continue
        if version!=5 or length<DIRECTORY_END-20 or length>BANK-20:
            raise ValueError('Unsupported player save for leaf migration')
        if save_sum(save[start+20:start+20+length])!=total:
            raise ValueError('Invalid save payload checksum')
        seed,index,count=struct.unpack_from('<3I',save,start+DIRECTORY)
        if (seed,index,count)!=(struct.unpack_from('<I',previous,36)[0],
                               struct.unpack_from('<I',previous,48)[0],4096):
            raise ValueError('Save does not belong to this seed')
        struct.pack_into('<I',updated,start+DIRECTORY+4,struct.unpack_from('<I',terrain,48)[0])
        for c in range(4096):
            pos=start+DIRECTORY+12+c*16
            slot,n,total,raw=struct.unpack_from('<4I',save,pos)
            if slot>2 or not 0<n<=CELLS or raw>1:raise ValueError('Invalid saved column')
            if not slot:
                if (n,total,raw)!=(CELLS,struct.unpack_from('<I',previous,512+c*4)[0],1):
                    raise ValueError('Invalid saved base column')
                record=(0,CELLS,struct.unpack_from('<I',terrain,512+c*4)[0],1)
            else:
                key=(c,slot);old_record=(n,total,raw)
                if key in converted:
                    prior,record=converted[key]
                    if prior!=old_record:raise ValueError('Save banks disagree about a terrain slot')
                else:
                    encoded=previous[offset(c,slot):offset(c,slot)+n]
                    if checksum(encoded)!=total:raise ValueError('Saved terrain slot is corrupt')
                    blocks=decode(encoded,raw);old_blocks=bytes(blocks)
                    base=terrain[offset(c):offset(c)+CELLS]
                    for i,b in enumerate(blocks):
                        if b&63==7:
                            flag=base[i]&RIM if base[i]&63==7 else RIM
                            blocks[i]=(b&~RIM)|flag
                    if old_blocks.translate(NORMALIZE)!=bytes(blocks).translate(NORMALIZE):
                        raise ValueError('Leaf migration changed an edited block')
                    payload,new_raw=encode(blocks)
                    merged[offset(c,slot):offset(c,slot)+CELLS]=payload+bytes(CELLS-len(payload))
                    record=(slot,len(payload),checksum(payload),new_raw)
                    converted[key]=(old_record,record)
            struct.pack_into('<4I',updated,pos,*record)
        struct.pack_into('<I',updated,start+8,save_sum(updated[start+20:start+20+length]))
    validate_save_preservation(save,updated)
    return bytes(merged),bytes(updated)


def upgrade_v2(previous,terrain,save):
    """Retain V2 pool pairs and both save banks while changing leaf metadata."""
    from convert_large_v2 import check_v2,decode as unpack
    check_v2(previous);check_v2(terrain)
    if previous[:48]!=terrain[:48] or struct.unpack_from('<I',terrain,92)[0]!=MAGIC:
        raise ValueError('Leaf migration requires the same V2 seed and geometry')
    old_pool,old_pairs=struct.unpack_from('<2I',previous,132)
    new_pool,new_pairs=struct.unpack_from('<2I',terrain,132)
    if old_pairs!=new_pairs:raise ValueError('V2 edit-pool capacity changed')
    def bases(data):
        index=struct.unpack_from('<I',data,124)[0]*512
        return [struct.unpack_from('<3I',data,index+c*12) for c in range(4096)]
    old_base,new_base=bases(previous),bases(terrain)
    old_light=struct.unpack_from('<I',previous,128)[0]*512
    new_light=struct.unpack_from('<I',terrain,128)[0]*512
    for c,((oa,on,os),(na,nn,ns)) in enumerate(zip(old_base,new_base)):
        if unpack(previous[oa:oa+on],CELLS).translate(NORMALIZE)!=unpack(terrain[na:na+nn],CELLS).translate(NORMALIZE):
            raise ValueError('Leaf migration would change terrain in column '+str(c))
        la,ln,ls=struct.unpack_from('<3I',previous,old_light+c*12)
        lb,lm,lt=struct.unpack_from('<3I',terrain,new_light+c*12)
        if previous[la:la+ln]!=terrain[lb:lb+lm]:
            raise ValueError('Leaf migration would change lighting')
    old_sum=checksum(b''.join(struct.pack('<I',b[2]) for b in old_base))
    new_sum=checksum(b''.join(struct.pack('<I',b[2]) for b in new_base))
    merged=bytearray(terrain);updated=bytearray(save);converted={};owners={}
    merged[new_pool*512:]=previous[old_pool*512:]
    for bank in range(2):
        start=bank*BANK
        magic,version,total,length,generation=struct.unpack_from('<5I',save,start)
        if magic!=0x46435354:continue
        if version!=5 or not DIRECTORY_END-20<=length<=BANK-20 or save_sum(save[start+20:start+20+length])!=total:
            raise ValueError('Invalid V2 save payload')
        seed,index,count=struct.unpack_from('<3I',save,start+DIRECTORY)
        if (seed,index,count)!=(struct.unpack_from('<I',previous,36)[0],old_sum,4096):
            raise ValueError('Save does not belong to this V2 seed')
        struct.pack_into('<I',updated,start+DIRECTORY+4,new_sum)
        for c in range(4096):
            pos=start+DIRECTORY+12+c*16
            field,n,total,raw=struct.unpack_from('<4I',save,pos)
            slot,pair=field&3,field>>2
            if slot>2 or not 0<n<=CELLS or raw>1 or (raw and n!=CELLS):
                raise ValueError('Invalid saved V2 column')
            if not slot:
                off,size,base_sum=old_base[c]
                if field or (n,total,raw)!=(size,base_sum,int(size==CELLS)):
                    raise ValueError('Invalid saved V2 base column')
                off,size,base_sum=new_base[c]
                record=(0,size,base_sum,int(size==CELLS))
            else:
                if pair>=old_pairs or (pair in owners and owners[pair]!=c):
                    raise ValueError('Invalid or shared V2 edit pair')
                owners[pair]=c
                key=(c,field);old_record=(n,total,raw)
                if key in converted:
                    prior,record=converted[key]
                    if prior!=old_record:raise ValueError('Save banks disagree about a V2 terrain slot')
                else:
                    old_at=(old_pool+(pair*2+slot-1)*24)*512
                    encoded=previous[old_at:old_at+n]
                    if checksum(encoded)!=total:raise ValueError('Saved V2 terrain slot is corrupt')
                    blocks=decode(encoded,raw);old_blocks=bytes(blocks)
                    off,size,_=new_base[c];base=unpack(terrain[off:off+size],CELLS)
                    for i,b in enumerate(blocks):
                        if b&63==7:
                            blocks[i]=(b&~RIM)|(base[i]&RIM if base[i]&63==7 else RIM)
                    if old_blocks.translate(NORMALIZE)!=bytes(blocks).translate(NORMALIZE):
                        raise ValueError('Leaf migration changed an edited block')
                    payload,new_raw=encode(blocks)
                    at=(new_pool+(pair*2+slot-1)*24)*512
                    merged[at:at+CELLS]=payload+bytes(CELLS-len(payload))
                    record=(field,len(payload),checksum(payload),new_raw)
                    converted[key]=(old_record,record)
            struct.pack_into('<4I',updated,pos,*record)
        struct.pack_into('<I',updated,start+8,save_sum(updated[start+20:start+20+length]))
    validate_save_preservation(save,updated)
    check_v2(merged)
    return bytes(merged),bytes(updated)

def validate_save_preservation(before,after,*,v2_slots=False):
    if len(before)!=len(after):raise ValueError('Save size changed')
    for bank in range(2):
        start=bank*BANK
        magic,version,total,length,generation=struct.unpack_from('<5I',before,start)
        if magic!=0x46435354:
            if before[start:start+BANK]!=after[start:start+BANK]:raise ValueError('Empty save bank changed')
            continue
        if before[start:start+8]!=after[start:start+8] or before[start+12:start+20]!=after[start+12:start+20] or \
           before[start+20:start+DIRECTORY]!=after[start+20:start+DIRECTORY] or \
           before[start+DIRECTORY_END:start+BANK]!=after[start+DIRECTORY_END:start+BANK]:
            raise ValueError('Player, inventory, chests, mobs or bank generation changed')
        if before[start+DIRECTORY:start+DIRECTORY+4]!=after[start+DIRECTORY:start+DIRECTORY+4] or \
           before[start+DIRECTORY+8:start+DIRECTORY+12]!=after[start+DIRECTORY+8:start+DIRECTORY+12]:
            raise ValueError('Saved seed or column count changed')
        for c in range(4096):
            p=start+DIRECTORY+12+c*16
            if v2_slots:
                old=struct.unpack_from('<I',before,p)[0];new=struct.unpack_from('<I',after,p)[0]
                if old!=(new&3) or (not old and new):raise ValueError('Saved terrain slot changed')
            elif before[p:p+4]!=after[p:p+4]:raise ValueError('Saved terrain slot changed')
        if struct.unpack_from('<I',after,start+8)[0]!=save_sum(after[start+20:start+20+length]):
            raise ValueError('Migrated save checksum is invalid')
