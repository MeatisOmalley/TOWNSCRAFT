"""Render the Low/Medium scale-2 view directly into Mode X plane-pair rows.

Applies after the raster-finish-skip overlay: the same odd-copy bands that
mark rows sharp gameplay overlays touch are expanded back into the chunky
framebuffer; every other view row reaches VGA from the presenter's plane
buffer without packing. Very Low, rain, menus and High keep the chunky path.
"""

from build_imported import replace_once

STATE = '''
/* DOS planar Low: logical pixel x of a scale-2 play frame goes straight into
   the presenter's Mode X rows (even x -> planes 0/1 byte x>>1, odd x -> planes
   2/3 at +PLANAR_C). One logical row spans two packed VGA rows (160 bytes). */
#define PLANAR_C 38400
#define PLANAR_ROW 160
static int rplanar,planarPolicy,planarFrame,planarFbPitch;
static u8 *planarFb,*planarSrc;
u8 dos_low_planar_rows[VIEW_H/2];
unsigned char *dos_video_planar_target(void);
void dos_video_planar_frame(const unsigned char *rows);
#ifdef DOS_VERY_LOW
extern int dos_very_low_mode;
#endif
void trap_opaqueP(Trap *t);
void trap_transpP(Trap *t);
void trap_opaquePw(Trap *t);
void trap_transpPw(Trap *t);
/* Indexed by flags (RP_TRANSPARENT | RP_WRAP) */
static void (*const trapFuncPlanar[4])(Trap *)=
{
	trap_opaqueP,trap_transpP,trap_opaquePw,trap_transpPw,
};
'''

SPAN = '''/* Planar flat span over logical pixels [xl,xr) of one row. */
static inline void planar_span(u8 *row,int xl,int xr,u8 color)
{
	int a=(xl+1)>>1,c=xl>>1;
	memset(row+a,color,((xr+1)>>1)-a);
	memset(row+PLANAR_C+c,color,(xr>>1)-c);
}

static void poly_scan('''

EXPAND = '''/* Expand one planar logical row into both chunky framebuffer rows. */
static void planar_expand_row(int y)
{
    u8 *dst=planarFb+y*2*planarFbPitch;
    const u8 *a=planarSrc+y*PLANAR_ROW,*c=a+PLANAR_C;
    int x;
    for(x=0; x<SCR_W/4; ++x)
    {
        dst[4*x]=dst[4*x+1]=a[x];
        dst[4*x+2]=dst[4*x+3]=c[x];
    }
    memcpy(dst+planarFbPitch,dst,SCR_W);
}

void raster_finish(void)
{
    int y;
    if(rplanar)
    {
        /* Sharp overlays draw over the chunky framebuffer after this; give
           them a correct background. All other rows stay planar. */
        for(y=0; y<rh; ++y)
        {
            dos_low_planar_rows[y]=!oddCopy[y];
            if(oddCopy[y]) planar_expand_row(y);
        }
        dos_video_planar_frame(dos_low_planar_rows);
        planarFrame=1;
        rplanar=0;
        rbuf=planarFb;
        rpitch=planarFbPitch;
        return;
    }
'''

COMPLETE = '''void raster_complete_odd_rows(void)
{
    int y;
    if(planarFrame)
    {
        /* The presenter's plane buffer is still intact until the next frame
           or a VGA verification reuses it. */
        for(y=0; y<VIEW_H/2; ++y)
            if(dos_low_planar_rows[y]) planar_expand_row(y);
        planarFrame=0;
        oddSkip=0;
        return;
    }
'''


def adapt(name, data):
    if name != 'raster.c':
        return data, []
    recipe = []
    text = data.decode('utf-8')
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n',
                           count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    text = replace_once(text, 'static int rw,rh,rpitch,rscale;\n',
        'static int rw,rh,rpitch,rscale;\n' + STATE, recipe)
    text = replace_once(text, '\trpitch=pitch;\n\trscale=scale;\n',
        '\trpitch=pitch;\n\trscale=scale;\n'
        '\trplanar=planarPolicy && 2==scale && SCR_W/2==w && VIEW_H/2==h;\n'
        '\tif(rplanar)\n\t{\n'
        '\t\tplanarFb=buf;\n\t\tplanarFbPitch=pitch;\n'
        '\t\trbuf=planarSrc=dos_video_planar_target();\n'
        '\t\trpitch=PLANAR_ROW/2;\n\t}\n', recipe)
    text = replace_once(text, 'static void poly_scan(', SPAN, recipe)
    text = replace_once(text, 'void (*trap)(Trap *)=',
        'void (*trap)(Trap *)=rplanar ? trapFuncPlanar[flags&(RP_TRANSPARENT|RP_WRAP)] : ', recipe)
    text = replace_once(text,
        '\t\t\t\t\t\tif(xr>xl) flat_span(row+xl*rscale,(xr-xl)*rscale,flatWord);\n',
        '\t\t\t\t\t\tif(xr>xl)\n\t\t\t\t\t\t{\n'
        '\t\t\t\t\t\t\tif(rplanar) planar_span(row,xl,xr,flat);\n'
        '\t\t\t\t\t\t\telse flat_span(row+xl*rscale,(xr-xl)*rscale,flatWord);\n'
        '\t\t\t\t\t\t}\n', recipe)
    text = replace_once(text,
        '\t\t\t\t\t\tmemset(row+xl*rscale,flat,(xr-xl)*rscale);\n',
        '\t\t\t\t\t\tif(rplanar) planar_span(row,xl,xr,flat);\n'
        '\t\t\t\t\t\telse memset(row+xl*rscale,flat,(xr-xl)*rscale);\n', recipe)
    text = replace_once(text,
        '\t\tif(1==rscale)\n\t\t{\n\t\t\trbuf[y*rpitch+x]=color;\n\t\t}\n',
        '\t\tif(rplanar)\n\t\t{\n'
        '\t\t\trbuf[y*PLANAR_ROW+(x&1)*PLANAR_C+(x>>1)]=color;\n\t\t}\n'
        '\t\telse if(1==rscale)\n\t\t{\n\t\t\trbuf[y*rpitch+x]=color;\n\t\t}\n', recipe)
    text = replace_once(text,
        '\t\tif(1==rscale)\n\t\t{\n\t\t\tmemset(rbuf+y*rpitch,color,rw);\n\t\t}\n',
        '\t\tif(rplanar)\n\t\t{\n'
        '\t\t\tmemset(rbuf+y*PLANAR_ROW,color,rw/2);\n'
        '\t\t\tmemset(rbuf+y*PLANAR_ROW+PLANAR_C,color,rw/2);\n\t\t}\n'
        '\t\telse if(1==rscale)\n\t\t{\n\t\t\tmemset(rbuf+y*rpitch,color,rw);\n\t\t}\n', recipe)
    text = replace_once(text, '    oddSkip=skip;\n',
        '    oddSkip=skip;\n'
        '    planarFrame=0;\n'
        '#ifdef DOS_VERY_LOW\n'
        '    planarPolicy=skip && !dos_very_low_mode;\n'
        '#else\n'
        '    planarPolicy=skip;\n'
        '#endif\n', recipe)
    text = replace_once(text, 'void raster_finish(void)\n{\n    int y;\n', EXPAND, recipe)
    text = replace_once(text, 'void raster_complete_odd_rows(void)\n{\n    int y;\n', COMPLETE, recipe)
    return text.encode('utf-8'), recipe
