"""Optional B8 distant heightmap and panorama, after renderer/world overlays."""
from build_imported import replace_once

def adapt(name,data,*,low_planar=False):
    if name not in ('world.c','column_hdd.inc','column_cache.inc','render.c','raster.c'):
        return data,[]
    text=data.decode().replace('\r\n','\n'); recipe=[]
    if b'\r\n' in data:
        recipe.append(dict(operation='replace',before='\r\n',after='\n',count=data.count(b'\r\n')))
    def one(before,after):
        nonlocal text
        text=replace_once(text,before,after,recipe)
    if name=='world.c':
        one('#include "hdd.h"','#include "hdd.h"\n#include "far_field.h"')
        one('static int cache_load(int cx,int cz,int slot);',
            'static int cache_load(int cx,int cz,int slot);\nstatic void far_world_header(const u32 *h);\nstatic void far_world_reset(void);\nstatic void far_world_column(int cx,int cz,const u8 *p);\nstatic void far_world_edit(int x,int z);')
        one('#include "large_world.inc"','#include "large_world.inc"\n#include "far_world.inc"')
        one('\tupdate_height(x,z);\n\trelight_edit(x,y,z,b);', '\tupdate_height(x,z);\n\tfar_world_edit(x,z);\n\trelight_edit(x,y,z,b);')
    elif name=='column_hdd.inc':
        # The light reader reuses columnScratch; read our header first.
        if 'stream_light_header(header);' in text:
            one('stream_light_header(header);','far_world_header(header);\n        stream_light_header(header);')
        else:
            one('\t\t/* Optional appendix headers. */\n', '\t\t/* Optional appendix headers. */\n\t\tfar_world_header(header);\n')
    elif name=='column_cache.inc':
        one('\tresidentReady[slot]=cacheBoot;', '\tresidentReady[slot]=cacheBoot;\n\tif(columnRecords[cz*g_NC+cx].off!=large_column_offset(cz*g_NC+cx,0))\n\t\tfar_world_column(cx,cz,g_blocks+slot*COLUMN_CELLS);')
    elif name=='render.c':
        one('#include "gfx.h"','#include "gfx.h"\n#include "far_field.h"')
        one('static void draw_sky(const RenderEnv *env)', '#include "far_render.inc"\n\nstatic void draw_sky(const RenderEnv *env)')
        one('\tdraw_clouds(env);','\tdraw_clouds(env);\n\tdraw_far_field(env);')
    else:
        one('void raster_pixel(int x,int y,u8 color)', '''#include "far_fill.h"
/* Resolve the active framebuffer once per ray, before entering its march. */
void raster_far_column(FarColumn *c,int x,int width,int top)
{
    c->mode=-1; c->top=top; c->x=x; c->scale=rscale;
    if((unsigned)x>=(unsigned)rw || x+width>rw) return;
''' + ('''    if(rplanar) {
        c->stride=PLANAR_ROW; c->mode=width==1 ? 9 : width==2 ? 10 : 12;
        c->cursor=rbuf+top*PLANAR_ROW+(x&1)*PLANAR_C+(x>>1);
        return;
    }
''' if low_planar else '') + '''    c->stride=rpitch*rscale; c->mode=width*rscale;
    c->cursor=rbuf+top*c->stride+x*rscale;
}
void raster_far_pattern(int x,int width,int y0,int y1,u8 a,u8 b,int phase)
{
    y0=MAX(0,y0); y1=MIN(rh,y1);
    if(y1<=y0) return;
    FarColumn c; raster_far_column(&c,x,width,y1);
    far_fill_column(&c,y0,a,b,phase);
}
/* Clipped visual-only heightmap strip; uses the active raster layout. */
void raster_far_strip(int x,int width,int y0,int y1,u8 color)
{
    if((unsigned)x>=(unsigned)rw) return;
    y0=MAX(0,y0); y1=MIN(rh,y1);
''' + ('''    if(rplanar) {
        u8 *p=rbuf+y0*PLANAR_ROW+(x&1)*PLANAR_C+(x>>1);
        u16 c=color|(color<<8);
        for(int y=y0;y<y1;++y,p+=PLANAR_ROW) {
            if(width==4) { *(u16 *)p=c; *(u16 *)(p+PLANAR_C)=c; }
            else { *p=color; if(width==2) p[PLANAR_C]=color; }
        }
        return;
    }
''' if low_planar else '') + '''    u32 c=(u32)color*0x01010101u;
    if(rscale==1) {
        u8 *p=rbuf+y0*rpitch+x;
        for(int y=y0;y<y1;++y,p+=rpitch) {
            if(width==8) { *(u32 *)p=c; *(u32 *)(p+4)=c; }
            else if(width==4) *(u32 *)p=c; else if(width==2) *(u16 *)p=c; else *p=color;
        }
    } else {
        u8 *p=rbuf+y0*2*rpitch+x*2;
        for(int y=y0;y<y1;++y,p+=rpitch*2) {
            if(width==4) { *(u32 *)p=c; *(u32 *)(p+4)=c; }
            else if(width==2) *(u32 *)p=c; else *(u16 *)p=c;
        }
    }
}

void raster_pixel(int x,int y,u8 color)''')
    return text.encode(),recipe
