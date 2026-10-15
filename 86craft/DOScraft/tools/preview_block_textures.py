"""Compile actual texture generators and make before/after review sheets."""
from pathlib import Path
import argparse
import subprocess
import tempfile
from PIL import Image, ImageDraw
import build_imported, dos_block_textures
ROOT=build_imported.ROOT
ZIG=ROOT.parents[1]/'fmtowns/townscraft/build/tools/zig-windows-x86_64-0.13.0/zig.exe'
NAMES=('Stone','Grass top','Grass side','Dirt','Cobble','Planks','Log side','Log top',
       'Leaves','Sand','Water','Glass','Bedrock','Coal ore','Iron ore','Craft top',
       'Craft side','Craft front','Furnace front','Furnace side','Furnace top','Torch',
       'Door bottom','Door top','Bed foot','Bed head','Bed side','Bed end','Wool','Gravel',
       'Flower','Tallgrass','Stone brick','TNT side','TNT top','Bed head side','Chest top',
       'Chest front','Gold ore','Diamond ore')

def generate(output):
    output=Path(output); output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='doscraft-textures-') as directory:
        folder=Path(directory); build_imported.stage_sources(build_imported.VENDOR,folder)
        src=folder/'src'; original=(src/'textures.c').read_bytes()
        import dos_chickens
        (src/'blocks.h').write_bytes(dos_chickens.adapt('blocks.h',(src/'blocks.h').read_bytes())[0])
        libc=(src/'libc.c').read_text(); start=libc.index('u32 hash3(')
        end=libc.index('\n}\n',start)+3
        (src/'texture_hash.inc').write_text(libc[start:end])
        (src/'textures.h').write_bytes(dos_block_textures.adapt('textures.h',(src/'textures.h').read_bytes())[0])
        render=dos_block_textures.adapt('render.c',(src/'render.c').read_bytes())[0].decode()
        start=render.index('#ifdef DOS_FAR_FIELD\nstatic void far_texture_changed(void);')
        end=render.index('void render_init(void)\n',start)
        (src/'texture_refresh.inc').write_text(render[start:end])
        far=(ROOT/'src/platform/dos/far_render.inc').read_text()
        start=far.index('static void far_texture_changed(void)');end=far.index('\n}\n',start)+3
        (src/'texture_far_refresh.inc').write_text(far[start:end])
        for mode in ('before','after'):
            data=original if mode=='before' else dos_block_textures.adapt('textures.c',original)[0]
            (src/'textures.c').write_bytes(data)
            exe=folder/(mode+'.exe'); raw=output/(mode+'.bin')
            subprocess.run(list(map(str,[ZIG,'cc','-target','x86-windows-gnu','-O2','-std=gnu99',
                *(['-DDOS_TEXTURE_CHECK'] if mode=='after' else []),'-I',src,'-I',ROOT/'src/platform/dos',
                ROOT/'tests/block_textures_native.c',src/'textures.c',src/'blocks.c','-o',exe])),check=True)
            subprocess.run([str(exe),str(raw.resolve())],check=True)
    draw_sheet(output)
    draw_sheet(output,('simple','after'),('Small view','Full resolution'),'resolution-styles.png')
    draw_tools(output)


def draw_tools(output):
    from compare_eagler_textures import names
    from import_eagler_textures import tool_material
    ids=names();indices=[i for i,n in enumerate(ids) if tool_material(n)]
    raws=[(output/(mode+'.bin')).read_bytes() for mode in ('simple','after')]
    sheet=Image.new('RGB',(840,65+((len(indices)+3)//4)*130),(26,29,33));d=ImageDraw.Draw(sheet)
    d.text((18,12),'16 x 16 TOOLS - Small view / Full resolution',(230,230,230))
    d.text((18,30),'Eaglercraft silhouettes, brown handles and material-specific heads.',(190,195,200))
    for slot,i in enumerate(indices):
        x=18+(slot%4)*210;y=62+(slot//4)*130
        d.text((x,y),ids[i][4:].replace('_',' ').title(),(235,235,235))
        for side,raw in enumerate(raws):
            tex=Image.frombytes('P',(16,16),raw[768+i*256:768+(i+1)*256]);tex.putpalette(raw[:768])
            rgb=tex.convert('RGB').resize((80,80),Image.Resampling.NEAREST)
            sheet.paste(rgb,(x+side*96,y+19))
            d.text((x+side*96,y+104),('Small view','Full resolution')[side],(175,180,185))
    sheet.save(output/'tool-styles.png')

def draw_sheet(output,modes=('before','after'),labels=('Before','After'),filename='comparison.png'):
    raws=[(output/(mode+'.bin')).read_bytes() for mode in modes]
    sheet=Image.new('RGB',(1060,1110),(26,29,33)); d=ImageDraw.Draw(sheet)
    d.text((18,12),'16 x 16 BLOCK TEXTURES - '+ ' / '.join(labels),(230,230,230))
    d.text((18,30),'Nearest-neighbor display. Palette and lighting ramps unchanged.',(190,195,200))
    for t,name in enumerate(NAMES):
        x=18+(t%5)*210; y=62+(t//5)*130
        d.text((x,y),name,(235,235,235))
        for index,raw in enumerate(raws):
            tex=Image.frombytes('P',(16,16),raw[768+t*256:768+(t+1)*256]); tex.putpalette(raw[:768])
            image=tex.convert('RGB').resize((80,80),Image.Resampling.NEAREST)
            sheet.paste(image,(x+index*96,y+19))
            d.text((x+index*96,y+104),labels[index],(175,180,185))
    sheet.save(output/filename)
    grass=Image.new('RGB',(768,384),(26,29,33))
    for side,raw in enumerate(raws):
        tex=Image.frombytes('P',(16,16),raw[768+256:768+512]);tex.putpalette(raw[:768])
        tile=tex.convert('RGB').resize((96,96),Image.Resampling.NEAREST)
        for y in range(4):
            for x in range(4):grass.paste(tile,(side*384+x*96,y*96))
    grass.save(output/('grass-tiled.png' if modes==('before','after') else 'grass-styles.png'))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    generate(p.parse_args().output)
