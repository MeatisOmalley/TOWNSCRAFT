"""Offline low-resolution art from the detailed silhouettes and compositions.

Only explicitly selected tiles are replaced. No palette/atlas allocation or
render-loop changes; the game copies these pixels at startup/style switching.
"""
from hashlib import sha256
import json
from pathlib import Path
from compare_eagler_textures import names
from import_eagler_textures import ROOT,tool_material

def p(r,s):return r*16+s

def generate(data):
    ids=names();tiles={};rules={}
    def original(name):
        i=ids.index(name);return list(data[i*256:(i+1)*256])
    def canvas(name,color):
        tiles[name]=[color]*256;return tiles[name]
    def rect(t,x0,y0,x1,y1,c):
        for y in range(y0,y1+1):
            for x in range(x0,x1+1):t[y*16+x]=c
    def mask(name,t):
        for i,c in enumerate(original(name)):
            if not c:t[i]=0

    # Same four inset panels/window layout and hinge side as the detailed door.
    for name in ('T_DOOR_BOTTOM','T_DOOR_TOP'):
        t=canvas(name,p(6,14))
        for y0,y1 in ((2,6),(8,12)):
            for x0,x1 in ((2,6),(8,12)):rect(t,x0,y0,x1,y1,p(6,11))
        # The upper half has window frames and a recessed lower rail.
        if name=='T_DOOR_TOP':
            rect(t,2,13,12,15,p(6,11));rect(t,3,14,11,15,p(6,14))
        else:rect(t,0,15,15,15,p(6,11))
        # Preserve the two metal hinges on each reference tile.
        rect(t,0,0 if name=='T_DOOR_BOTTOM' else 4,0,1 if name=='T_DOOR_BOTTOM' else 5,p(0,12))
        if name=='T_DOOR_BOTTOM':rect(t,0,10,0,11,p(0,12))
        else:rect(t,0,15,0,15,p(0,12))
        mask(name,t)
        rules[name]='light brown frame, dark inset panels; detailed window silhouette; gray hinges'

    t=canvas('T_STONEBRICK',p(0,11))
    for y in range(16):
        for x in range(16):
            if y in (7,15) or x==(0 if y<8 else 8):t[y*16+x]=p(0,10)
    rules['T_STONEBRICK']='two colors only: flat bricks and staggered mortar'

    t=canvas('T_CRAFT_TOP',p(1,15))
    rect(t,0,0,15,0,p(5,7));rect(t,0,15,15,15,p(5,7))
    rect(t,0,0,0,15,p(5,7));rect(t,15,0,15,15,p(5,7))
    rect(t,3,3,12,12,p(1,11))
    for y in (4,7,10):
        for x in (4,7,10):rect(t,x,y,x+1,y+1,p(1,15))
    for x,y in ((1,1),(12,1),(1,12),(12,12)):rect(t,x,y,x+2,y+2,p(6,14))
    rules['T_CRAFT_TOP']='same inset 3x3 work grid and reinforced corners; solid fills'
    for name in ('T_CRAFT_SIDE','T_CRAFT_FRONT'):
        t=canvas(name,p(6,15))
        for y in (3,7,11,15):rect(t,1,y,14,y,p(6,11))
        rect(t,0,0,0,15,p(5,7));rect(t,15,0,15,15,p(5,7))
        rect(t,7,0,8,15,p(5,7))
        src=original(name)
        # Retain the actual hanging tools, flattening their internal grain.
        for i,c in enumerate(src):
            x,y=i%16,i//16
            if x in (0,7,8,15):continue
            if c//16==0:t[i]=p(0,14)
            elif c//16 in (11,5) and y<13:t[i]=p(5,9)
        rules[name]='detailed plank/frame/tool placement, without wood grain or tool highlights'

    t=canvas('T_BED_FOOT_TOP',p(9,14));rect(t,0,13,15,15,p(9,15))
    rules['T_BED_FOOT_TOP']='flat bright red blanket with one broad folded edge'
    t=canvas('T_BED_HEAD_TOP',p(9,14))
    rect(t,0,0,15,6,p(0,15));rect(t,0,7,15,7,p(0,14));rect(t,0,8,15,8,p(9,12))
    for i,c in enumerate(original('T_BED_HEAD_TOP')[:128]):
        t[i]=p(0,15 if c==15 else 14)
    rules['T_BED_HEAD_TOP']='same full-width white pillow and red blanket; no speckles'
    for name in ('T_BED_SIDE','T_BED_END','T_BED_HEAD_SIDE'):
        src=original(name);t=canvas(name,0)
        for i,c in enumerate(src):
            if not c:continue
            if c//16==9:t[i]=p(9,14)
            elif c//16==0:t[i]=p(0,15)
            else:t[i]=p(6,12)
        rules[name]='same mattress/pillow/frame silhouette; flat red, white and wood'

    # Quiet stone cap with broad connected insets, rather than per-pixel grain.
    t=canvas('T_FURNACE_TOP',p(0,11))
    rect(t,0,0,15,0,p(0,8));rect(t,0,15,15,15,p(0,8))
    rect(t,0,0,0,15,p(0,8));rect(t,15,0,15,15,p(0,8))
    src=original('T_FURNACE_TOP')
    for y in range(1,15):
        for x in range(1,15):
            values=[src[yy*16+xx]&15 for yy in range(max(1,y-1),min(14,y+1)+1)
                for xx in range(max(1,x-1),min(14,x+1)+1)]
            t[y*16+x]=p(0,12 if sum(values)*10>=len(values)*106 else 11)
    rules['T_FURNACE_TOP']='reference stone border/facets, smoothed offline to remove pixel grain'
    src=original('T_FLOWER');t=canvas('T_FLOWER',0)
    for i,c in enumerate(src):
        if c:t[i]=p(9,12 if (c&15)<14 else 14) if c//16==9 else p(2,11)
    rules['T_FLOWER']='same poppy silhouette, red petals and green stem/leaves'

    for name in ids:
        material=tool_material(name)
        if not material:continue
        src=original(name);t=canvas(name,0)
        for i,c in enumerate(src):
            if not c:continue
            ramp,shade=c//16,c&15
            if ramp in (5,6):t[i]=p(5,8) if shade<9 else p(6,12)
            elif material=='DIAMOND':t[i]=p(12,10) if shade<9 else p(12,15)
            else:t[i]=p(0,7) if shade<10 else p(0,12 if material=='STONE' else 15)
        rules[name]='actual detailed/reference silhouette; two wood shades and two head shades'
    return [(ids.index(n),n,bytes(t),rules[n]) for n,t in tiles.items()]

def write(output=None):
    output=Path(output or ROOT/'assets/textures');output.mkdir(parents=True,exist_ok=True)
    full=(ROOT/'assets/textures/eagler_full.bin').read_bytes();rows=generate(full)
    lines=['/* Generated by tools/simplify_textures.py. Offline simple art. */',
        'static const u8 dos_simple_ids[]={'+','.join(n for i,n,t,r in rows)+'};',
        'static const u8 dos_simple_tiles[][256]={']
    for i,n,t,r in rows:
        lines.append('    { /* '+n+' */')
        lines.extend('        '+','.join(map(str,t[y*16:(y+1)*16]))+',' for y in range(16))
        lines.append('    },')
    lines.extend(['};','static void dos_tex_import_simple(void)','{',
        '    for(unsigned i=0;i<sizeof(dos_simple_ids);++i)',
        '        memcpy(g_tex[dos_simple_ids[i]],dos_simple_tiles[i],256);','}',''])
    (output/'eagler_simple.inc').write_text('\n'.join(lines))
    (output/'eagler_simple.json').write_text(json.dumps({'full_source_sha256':sha256(full).hexdigest(),
        'tiles':[{'texture':n,'index':i,'rule':r,'sha256':sha256(t).hexdigest(),
        'colors':len(set(t)),'transparent_pixels':t.count(0)} for i,n,t,r in rows]},indent=2)+'\n')
    print('Simplified',len(rows),'tiles')

if __name__=='__main__':write()
