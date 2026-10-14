"""Offline visibility-metadata migration; preserve all logical blocks and saves."""
import struct
from bake_large_world import checksum

MAGIC=0x3246414c
RIM=64
CELLS=12288
NORMALIZE=bytes(b&~RIM if b&31==7 else b for b in range(256))
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
                        if b&31==7:
                            flag=base[i]&RIM if base[i]&31==7 else RIM
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

def validate_save_preservation(before,after):
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
            if before[p:p+4]!=after[p:p+4]:raise ValueError('Saved terrain slot changed')
        if struct.unpack_from('<I',after,start+8)[0]!=save_sum(after[start+20:start+20+length]):
            raise ValueError('Migrated save checksum is invalid')
