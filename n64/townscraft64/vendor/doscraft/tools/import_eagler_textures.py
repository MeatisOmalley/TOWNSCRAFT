"""Import actual pixels from the user's local Eaglercraft EPK into DOS tiles.

Conversion is offline. No PNG/EPK decoder, new palette, extra atlas, animation,
or disk access is added to the game. Grass top retains the user's original art.
"""
import argparse
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
from PIL import Image
from compare_eagler_textures import resources, names, reference, tint

ROOT=Path(__file__).resolve().parents[1]
# Entity UV rectangles describe real faces, not resized complete skin sheets.
CROPS={
    'T_PIG_SKIN':(28,16,38,32), 'T_PIG_FACE':(8,8,16,16),
    'T_SHEEP_FACE':(8,8,14,14), 'T_ZOMBIE_SKIN':(8,0,16,8),
    'T_ZOMBIE_FACE':(8,8,16,16), 'T_SHIRT':(20,20,28,32),
    'T_PANTS':(4,20,8,32), 'T_CREEPER_SKIN':(20,20,28,32),
    'T_CREEPER_FACE':(8,8,16,16), 'T_PLAYER_SKIN':(44,28,48,32),
    'T_CHICKEN_SKIN':(6,15,12,23), 'T_CHICKEN_FACE':(3,3,7,9),
    'T_CHICKEN_YELLOW':(16,0,20,2),
}


def palette_bytes():
    # Parse the pinned palette definition, instead of maintaining another palette.
    import re
    source=(ROOT/'vendor/towns/src/textures.c').read_text()
    start=source.index('static const u8 rampRGB');end=source.index('/* ---- drawing helpers ---- */',start)
    colors=[tuple(map(int,m)) for m in re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}',source[start:end])]
    bright=list(map(int,re.search(r'shadeBright\[16\]=\s*\{([^}]+)',source[start:end]).group(1).split(',')))
    assert len(colors)==len(bright)==16
    return bytes(min(255,c*b//256) for color in colors for b in bright for c in color)


def source_tile(name,index,files):
    path,kind=reference(name,index); rule='source alpha'; crop=None; color=None
    if name=='T_SHEEP_WOOL':path='assets/minecraft/textures/block/white_wool.png';kind='tile'
    image=Image.open(BytesIO(files[path])).convert('RGBA')
    if name=='T_I_BED':
        # This bundle has model tiles, not a flat bed item PNG.
        icon=Image.new('RGBA',(16,16))
        icon.paste(image.resize((13,6),Image.Resampling.NEAREST),(1,6))
        pillow=Image.open(BytesIO(files['assets/minecraft/textures/block/red_bed_head_up.png'])).convert('RGBA')
        icon.paste(pillow.crop((0,0,16,8)).resize((4,6),Image.Resampling.NEAREST),(1,6))
        side=Image.open(BytesIO(files['assets/minecraft/textures/block/red_bed_foot_south.png'])).convert('RGBA')
        icon.paste(side.resize((13,4),Image.Resampling.NEAREST),(1,12));image=icon
        rule='compact icon composed from source bed top, pillow and end tiles'
    elif name=='T_CHEST_TOP':
        crop=(14,0,28,14);image=image.crop(crop)
    elif name=='T_CHEST_FRONT':
        # Chest lid and base have separate UV islands. Join their front faces.
        lid=image.crop((14,14,28,19));base=image.crop((14,33,28,43))
        front=Image.new('RGBA',(14,15));front.paste(lid,(0,0));front.paste(base,(0,5))
        # The latch is a separate cuboid on the reference model.
        front.alpha_composite(image.crop((2,1,4,5)),(6,3));image=front
        rule='joined chest front UVs and latch'
    elif name in CROPS:
        crop=CROPS[name];image=image.crop(crop)
    else:
        crop=(0,0,image.width,image.width);image=image.crop(crop) # First animation frame.
    image=image.resize((16,16),Image.Resampling.NEAREST)
    if name=='T_PIG_FACE':
        snout=Image.open(BytesIO(files[path])).convert('RGBA').crop((17,17,21,20))
        image.alpha_composite(snout.resize((8,6),Image.Resampling.NEAREST),(4,8))
        rule='source head face plus source snout front UV'
    elif name=='T_CHICKEN_FACE':
        # Move the side-eye pixels onto our simple cuboid's front face.
        sheet=Image.open(BytesIO(files[path])).convert('RGBA')
        eye=sheet.crop((2,4,3,5)).resize((2,3),Image.Resampling.NEAREST)
        image.alpha_composite(eye,(2,5));image.alpha_composite(eye,(12,5))
        rule='source head front and side-eye pixels'
    if name in ('T_LEAVES','T_TALLGRASS'):
        color=(55,135,40) if name=='T_LEAVES' else (105,180,55);image=tint(image,color)
    elif name=='T_WATER':color=(55,95,230);image=tint(image,color)
    elif name=='T_SAND':
        # A slight exposure adjustment keeps the source grain visible when
        # the brightest reference colors would all quantize to one shade.
        image=tint(image,(240,240,240));rule+='; 240/255 exposure for palette detail'
    elif name=='T_GRASS_SIDE':
        overlay=Image.open(BytesIO(files['assets/minecraft/textures/block/grass_block_side_overlay.png'])).convert('RGBA')
        image.alpha_composite(tint(overlay,(105,180,55)))
        rule='source soil plus tinted source grass overlay'
    return image,dict(reference=path,reference_sha256=sha256(files[path]).hexdigest(),
        crop=crop,tint=color,rule=rule)


def ramps_for(name):
    if tool_material(name):
        return (5,6) if tool_material(name)=='WOOD' else (0,5,6,12) if tool_material(name)=='DIAMOND' else (0,5,6)
    if name in ('T_DIRT',):return (1,)
    if name=='T_LEAVES':return (3,)
    if name=='T_TALLGRASS':return (2,)
    if name=='T_SAND':return (4,)
    if name=='T_WATER':return (7,)
    if name in ('T_STONE','T_COBBLE','T_BEDROCK','T_WOOL','T_SHEEP_WOOL','T_STONEBRICK',
                'T_FURNACE_SIDE','T_FURNACE_TOP','T_CRACK'):return (0,)
    if name in ('T_PLANKS','T_LOG_SIDE','T_LOG_TOP'):return (1,5,6)
    if name in ('T_CHEST_TOP','T_CHEST_FRONT'):return (0,1,5,6)
    return tuple(range(16))


def tool_material(name):
    if not name.startswith('T_I_'): return None
    parts=name[4:].split('_')
    return parts[0] if len(parts)==2 and parts[1] in ('PICK','SWORD','AXE','SHOVEL') else None


def convert(assets):
    files=resources(assets); ids=names(); palette=palette_bytes()
    rgb=[tuple(palette[i:i+3]) for i in range(0,768,3)]
    data=bytearray(len(ids)*256);rows=[]
    for index,name in enumerate(ids):
        if name=='T_GRASS_TOP':continue
        image,record=source_tile(name,index,files)
        candidates=[r*16+s for r in ramps_for(name) for s in range(1,16)]
        mapped={};tile=[]
        for r,g,b,a in image.get_flattened_data():
            if name=='T_LEAVES' and a<128:
                # Keep foliage opaque as before; source holes become dark gaps.
                tile.append(3*16+6);continue
            if a<128:tile.append(0);continue
            pixel=(r,g,b)
            if pixel not in mapped:
                options=candidates
                if name=='T_GRASS_SIDE':
                    options=[ramp*16+s for ramp in ((2,) if g>r else (1,)) for s in range(1,16)]
                material=tool_material(name)
                if material:
                    # Keep wooden handles brown; unrestricted nearest-color
                    # mapping had sent them into the olive ramp.
                    wood=material=='WOOD' or (r>g*1.12 and g>b*1.12)
                    ramps=(5,6) if wood else (0,12) if material=='DIAMOND' else (0,)
                    options=[ramp*16+s for ramp in ramps for s in range(1,16)]
                mapped[pixel]=min(options,key=lambda i:sum(w*(v-c)**2 for w,v,c in zip((2,4,3),pixel,rgb[i])))
            tile.append(mapped[pixel])
        data[index*256:(index+1)*256]=bytes(tile)
        record.update(texture=name,index=index,allowed_ramps=list(ramps_for(name)),
            converted_sha256=sha256(bytes(tile)).hexdigest(),colors=len(set(tile)),transparent_pixels=tile.count(0))
        if name=='T_LEAVES':record['rule']+='; alpha holes filled dark green'
        if name=='T_DIRT':record['rule']+='; grey flecks converted to brown'
        if tool_material(name):record['rule']+='; wooden handle/head colors constrained to brown, other heads to their material'
        rows.append(record)
    return bytes(data),dict(assets_sha256=sha256(Path(assets).read_bytes()).hexdigest(),
        palette_sha256=sha256(palette).hexdigest(),conversion='nearest existing ramp shade; no dithering',
        retained=['T_GRASS_TOP'],tiles=rows)


def write(assets,output=None):
    output=Path(output or ROOT/'assets/textures');output.mkdir(parents=True,exist_ok=True)
    data,metadata=convert(assets)
    (output/'eagler_full.bin').write_bytes(data)
    (output/'eagler_full.json').write_text(json.dumps(metadata,indent=2)+'\n')
    lines=['/* Generated by tools/import_eagler_textures.py from the supplied EPK. */',
        '/* Actual source pixels, palette-converted offline. Custom grass top retained. */',
        'static const u8 dos_eagler_ids[]={'+','.join(r['texture'] for r in metadata['tiles'])+'};',
        'static const u8 dos_eagler_tiles[][256]={']
    for record in metadata['tiles']:
        tile=data[record['index']*256:(record['index']+1)*256]
        lines.append('    { /* '+record['texture']+' */')
        lines.extend('        '+','.join(str(c) for c in tile[row*16:(row+1)*16])+',' for row in range(16))
        lines.append('    },')
    lines.extend(['};','static void dos_tex_import_full(void)','{',
        '    for(unsigned i=0;i<sizeof(dos_eagler_ids);++i)',
        '        memcpy(g_tex[dos_eagler_ids[i]],dos_eagler_tiles[i],256);','}',''])
    (output/'eagler_full.inc').write_text('\n'.join(lines))
    print('Imported',len(metadata['tiles']),'actual reference tiles; custom grass top retained')
    return metadata


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,required=True);parser.add_argument('--output',type=Path)
    args=parser.parse_args();write(args.assets,args.output)
