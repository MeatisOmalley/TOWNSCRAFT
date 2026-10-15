"""Reference-only contact sheets from the supplied local EPK resources.

Animated references show their first frame; model-only references are labeled
as sheets or face crops. Actual full-resolution import is handled separately
by import_eagler_textures.py; this tool only creates comparison sheets.
"""
import argparse
from io import BytesIO
import json
from pathlib import Path
import re
import struct
from PIL import Image,ImageDraw
from preview_block_textures import ROOT

BLOCKS=('stone','grass_block_top','grass_block_side','dirt','cobblestone','oak_planks',
 'oak_log','oak_log_top','oak_leaves','sand','water_still','glass','bedrock','coal_ore',
 'iron_ore','crafting_table_top','crafting_table_side','crafting_table_front',
 'furnace_front_on','furnace_side','furnace_top','torch','oak_door_bottom','oak_door_top',
 'red_bed_foot_up','red_bed_head_up','red_bed_foot_east','red_bed_foot_south',
 'white_wool','gravel','poppy','short_grass','stone_bricks',
 'tnt_side','tnt_top','red_bed_head_east','CHEST','CHEST','gold_ore','diamond_ore')
ITEMS={'STICK':'stick','COAL':'coal','IRON':'iron_ingot','PORK':'porkchop',
 'CHICKEN':'chicken','COOKED_CHICKEN':'cooked_chicken','FEATHER':'feather',
 'COOKED_PORK':'cooked_porkchop','MUTTON':'mutton','COOKED_MUTTON':'cooked_mutton',
 'FLESH':'rotten_flesh','GUNPOWDER':'gunpowder','DOOR':'oak_door','BED':'red_bed',
 'APPLE':'apple','GOLD':'gold_ingot','DIAMOND':'diamond','GOLDEN_APPLE':'golden_apple'}
MOBS={'T_PIG_SKIN':'pig/pig_temperate','T_PIG_FACE':'pig/pig_temperate',
 'T_CHICKEN_SKIN':'chicken/chicken_temperate','T_CHICKEN_FACE':'chicken/chicken_temperate',
 'T_CHICKEN_YELLOW':'chicken/chicken_temperate',
 'T_SHEEP_WOOL':'sheep/sheep_wool','T_SHEEP_FACE':'sheep/sheep',
 'T_ZOMBIE_SKIN':'zombie/zombie','T_ZOMBIE_FACE':'zombie/zombie',
 'T_SHIRT':'zombie/zombie','T_PANTS':'zombie/zombie',
 'T_CREEPER_SKIN':'creeper/creeper','T_CREEPER_FACE':'creeper/creeper',
 'T_PLAYER_SKIN':'player/wide/steve'}

def resources(path):
    data=Path(path).read_bytes();at=0;files={}
    while at<len(data):
        tag=data[at:at+4];at+=4
        if tag==b'END$': break
        if tag not in (b'HEAD',b'FILE'):raise ValueError('Unexpected EPK record')
        length=data[at];at+=1;name=data[at:at+length].decode('utf8');at+=length
        size=struct.unpack_from('>I',data,at)[0];at+=4;payload=data[at:at+size];at+=size
        if data[at:at+1]!=b'>':raise ValueError('Invalid EPK terminator')
        at+=1
        if tag==b'FILE':
            if payload[-1:]!=b':':raise ValueError('Invalid file record')
            files[name]=payload[4:-1]
    return files

def names():
    import dos_chickens
    text=dos_chickens.adapt('blocks.h',(ROOT/'vendor/towns/src/blocks.h').read_bytes())[0].decode()
    begin=text.index('T_STONE,');end=text.index('NUM_TEXTURES',begin)
    return re.findall(r'\bT_[A-Z0-9_]+\b',re.sub(r'/\*.*?\*/','',text[begin:end],flags=re.S))

def reference(name,index):
    prefix='assets/minecraft/textures/'
    if index<len(BLOCKS):
        block=BLOCKS[index]
        if block=='CHEST':return prefix+'entity/chest/normal.png','model sheet'
        return prefix+'block/'+block+'.png','tile'
    if name in MOBS:
        return prefix+'entity/'+MOBS[name]+'.png','face crop' if name.endswith('_FACE') else 'skin sheet'
    if name.startswith('T_I_'):
        item=name[4:]
        if item=='BED':return prefix+'block/red_bed_foot_up.png','model tile'
        if item=='TORCH':return prefix+'block/torch.png','tile'
        if item in ITEMS:return prefix+'item/'+ITEMS[item]+'.png','tile'
        item=item.lower().replace('wood_','wooden_').replace('stone_','stone_').replace('gold_','golden_')
        return prefix+'item/'+item.replace('_pick','_pickaxe')+'.png','tile'
    if name=='T_CRACK':return prefix+'block/destroy_stage_4.png','tile'
    return '', 'no equivalent'

def tint(image,rgb):
    image=image.convert('RGBA'); pixels=[]
    for r,g,b,a in image.get_flattened_data():pixels.append((r*rgb[0]//255,g*rgb[1]//255,b*rgb[2]//255,a))
    image.putdata(pixels);return image

def make(raw_path,assets_path,output):
    raw=Path(raw_path).read_bytes();files=resources(assets_path);ids=names()
    if len(raw)!=768+len(ids)*256:raise ValueError('Texture count mismatch')
    output=Path(output);output.mkdir(parents=True,exist_ok=True);rows=[]
    split=ids.index('T_I_STICK')
    for group,indices in (('blocks',range(40)),('mobs',range(40,split)),('items',range(split,len(ids)))):
        indices=list(indices);sheet=Image.new('RGB',(1040,65+((len(indices)+4)//5)*133),(26,29,33));draw=ImageDraw.Draw(sheet)
        draw.text((14,10),'DOSCRAFT / EAGLERCRAFT REFERENCE - '+group.upper(),(235,235,235))
        draw.text((14,28),'Original game art at left. Tinted references; first animation frame. Model sheets labeled.',(180,185,190))
        for slot,index in enumerate(indices):
            name=ids[index];path,kind=reference(name,index);x=14+(slot%5)*205;y=60+(slot//5)*133
            draw.text((x,y),name[2:].replace('_',' ').title(),(235,235,235))
            ours=Image.frombytes('P',(16,16),raw[768+index*256:768+(index+1)*256]);ours.putpalette(raw[:768])
            sheet.paste(ours.convert('RGB').resize((80,80),Image.Resampling.NEAREST),(x,y+18))
            note=kind
            if path in files:
                ref=Image.open(BytesIO(files[path])).convert('RGBA')
                if kind=='face crop':ref=ref.crop((8,8,16,16))
                elif kind=='tile':ref=ref.crop((0,0,ref.width,ref.width))
                if name in ('T_GRASS_TOP','T_LEAVES','T_TALLGRASS'):ref=tint(ref,(55,135,40) if name=='T_LEAVES' else (105,180,55));note+=' tinted'
                if name=='T_WATER':ref=tint(ref,(55,95,230));note+=' tinted'
                if name=='T_GRASS_SIDE':
                    overlay='assets/minecraft/textures/block/grass_block_side_overlay.png'
                    if overlay in files:ref.alpha_composite(tint(Image.open(BytesIO(files[overlay])),(105,180,55)))
                background=Image.new('RGBA',ref.size,(26,29,33,255));background.alpha_composite(ref)
                scale=min(80/ref.width,80/ref.height)
                background=background.resize((round(ref.width*scale),round(ref.height*scale)),Image.Resampling.NEAREST)
                sheet.paste(background.convert('RGB'),(x+95+(80-background.width)//2,y+18+(80-background.height)//2))
            else:
                note='not present';draw.text((x+98,y+45),'No direct asset',(160,160,160))
            draw.text((x,y+103),'Ours',(175,180,185));draw.text((x+95,y+103),note,(175,180,185))
            rows.append({'texture':name,'reference':path,'kind':kind,'available':path in files})
        sheet.save(output/(group+'.png'))
    (output/'mapping.json').write_text(json.dumps(rows,indent=2)+'\n')
    print('Compared',len(rows),'textures;',sum(r['available'] for r in rows),'reference assets found')
    print('Missing:',[r['texture'] for r in rows if not r['available']])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--raw',type=Path,required=True)
    p.add_argument('--assets',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();make(a.raw,a.assets,a.output)
