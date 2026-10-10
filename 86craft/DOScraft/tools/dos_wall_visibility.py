"""Optional directional wall-cone rejection before quad projection/subdivision."""
from build_imported import replace_once

FRONTEND=r'''
#define WALL_PATCHES 16
typedef struct { int axis,plane,owner,lo[3],hi[3]; u32 key; } WallPatch;
static WallPatch wallPatches[WALL_PATCHES];
static DosWallCone wallCones[WALL_PATCHES];
static int nWallPatches,nWallCones,wallCamera[3];
static int wallDisabled;
unsigned dos_wall_sources,dos_wall_tests,dos_wall_rejected;

static int wall_patch_ready(const WallPatch *p)
{
    int lo[3],hi[3],x,y,z,a;
    for(a=0;a<3;++a) {
        lo[a]=(a==p->axis?p->owner:p->lo[a])/(256*CS);
        hi[a]=(a==p->axis?p->owner:p->hi[a]-1)/(256*CS);
    }
    for(z=lo[2];z<=hi[2];++z) for(x=lo[0];x<=hi[0];++x) {
        if(!world_column_sim_ready(x,z)) return 0;
        for(y=lo[1];y<=hi[1];++y) {
            Chunk *ch=&g_chunks[chunk_index(x,y,z)];
            if(!ch->meshed || ch->dirty || ch->dirtyLayers || ch->trimmed) return 0;
        }
    }
    return 1;
}

static void wall_select(void)
{
    int i;
    nWallPatches=0;
    /* Source only already-collected quads, so emit/list overflow or streaming
       cannot fabricate a wall. Keep the unculled painter list intact. */
    for(i=0;i<nStatic && i<256;++i) {
        const Item *it=&items[order[i]];
        WallPatch p;
        int x,y,z,dir,ex,ez,j,a,merged=0;
        Chunk *ch;
        if(((it->a>>25)&3)!=IK_QUAD || texTransparent[it->b&255]) continue;
        x=it->a&255; y=(it->a>>16)&63; z=(it->a>>8)&255; dir=(it->a>>22)&7;
        if(!world_column_sim_ready(x/CS,z/CS)) continue;
        ch=&g_chunks[chunk_index(x/CS,y/CS,z/CS)];
        if(!ch->meshed || ch->dirty || ch->dirtyLayers || ch->trimmed) continue;
        ex=((it->b>>16)&15)+1; ez=((it->b>>20)&15)+1;
        p.lo[0]=x*256; p.lo[1]=y*256; p.lo[2]=z*256;
        p.hi[0]=(x+ex)*256; p.hi[1]=(y+1)*256; p.hi[2]=(z+ez)*256;
        p.axis=dir/2; p.plane=(dir&1)?p.hi[p.axis]:p.lo[p.axis];
        p.owner=p.plane-((dir&1)?256:0);
        p.lo[p.axis]=p.hi[p.axis]=p.plane; p.key=itemKey[order[i]];
        /* Only merge exact rectangular unions, never bridge a doorway/hole.
           Different opaque textures/light levels may share a visibility proof. */
        for(j=0;j<nWallPatches && !merged;++j) {
            WallPatch *q=&wallPatches[j];
            if(p.axis!=q->axis || p.plane!=q->plane || p.owner!=q->owner) continue;
            for(a=0;a<3;++a) if(a!=p.axis) {
                int b=3-p.axis-a;
                if(p.lo[b]==q->lo[b] && p.hi[b]==q->hi[b] &&
                   p.lo[a]<=q->hi[a] && p.hi[a]>=q->lo[a]) {
                    q->lo[a]=MIN(q->lo[a],p.lo[a]); q->hi[a]=MAX(q->hi[a],p.hi[a]);
                    q->key=MAX(q->key,p.key); merged=1; break;
                }
            }
        }
        if(!merged && nWallPatches<WALL_PATCHES) wallPatches[nWallPatches++]=p;
    }
}
static void wall_prepare(void)
{
    int i;
    nWallCones=0; dos_wall_tests=dos_wall_rejected=0;
    wallCamera[0]=g_cam.x; wallCamera[1]=g_cam.y; wallCamera[2]=g_cam.z;
    for(i=0;i<nWallPatches;++i) {
        WallPatch *p=&wallPatches[i];
        int k,near=1;
        if(!wall_patch_ready(p)) continue;
        /* All actual transformed corners must survive near clipping. */
        for(k=0;k<8;++k) {
            int x=(k&1?p->hi[0]:p->lo[0])/256;
            int y=(k&2?p->hi[1]:p->lo[1])/256;
            int z=(k&4?p->hi[2]:p->lo[2])/256;
            if(TX[x].c[2]+TY[y].c[2]+TZ[z].c[2]<64) { near=0; break; }
        }
        if(near && dos_wall_cone(&wallCones[nWallCones],p->axis,p->plane,p->lo,p->hi,wallCamera,p->key))
            ++nWallCones;
    }
    dos_wall_sources=nWallCones;
}
static int wall_hidden_item(const Item *it,u32 key)
{
    int lo[3],hi[3],i;
    if(wallDisabled || !nWallCones || ((it->a>>25)&3)!=IK_QUAD) return 0;
    lo[0]=(it->a&255)*256; lo[1]=((it->a>>16)&63)*256; lo[2]=((it->a>>8)&255)*256;
    hi[0]=lo[0]+(((it->b>>16)&15)+1)*256;
    hi[1]=lo[1]+256; hi[2]=lo[2]+(((it->b>>20)&15)+1)*256;
    for(i=0;i<nWallCones;++i) {
        ++dos_wall_tests;
        if(dos_wall_hidden(&wallCones[i],lo,hi,wallCamera,key)) { ++dos_wall_rejected; return 1; }
    }
    return 0;
}
'''

def adapt(name,data,verify=False):
    if name!='render.c': return data,[]
    recipe=[]; text=data.decode('utf-8')
    if '\r\n' in text:
        recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
        text=text.replace('\r\n','\n')
    text=replace_once(text,'#include "gfx.h"\n','#include "gfx.h"\n#include "wall_visibility.h"\n',recipe)
    text=replace_once(text,'static u32 entKey[MAX_BOXES];\n',FRONTEND+'\nstatic u32 entKey[MAX_BOXES];\n',recipe)
    text=replace_once(text,'\t\t\tnStatic=nItems;\n','\t\t\tnStatic=nItems;\n\t\t\twall_select();\n',recipe)
    text=replace_once(text,'\tbuild_occlusion();\n','\twall_prepare();\n\tbuild_occlusion();\n',recipe)
    text=replace_once(text,'\t\tit=&items[order[key--]];\n',
        '\t\tit=&items[order[key--]];\n\t\tif(wall_hidden_item(it,occCurrentKey)) continue;\n',recipe)
    if verify:
        text=replace_once(text,'void render_frame(u8 *fb,const RenderEnv *env)\n',
            'static void wall_render_frame(u8 *fb,const RenderEnv *env)\n',recipe)
        tail='''
/* Diagnostic double-render oracle, never timing/adoption evidence. */
unsigned dos_wall_verify_frames,dos_wall_verify_failures;
static u8 wallExpected[FB_PITCH*VIEW_H];
void render_frame(u8 *fb,const RenderEnv *env)
{
    unsigned sources,tests,rejected;
    wallDisabled=0;
    wall_render_frame(fb,env);
    sources=dos_wall_sources; tests=dos_wall_tests; rejected=dos_wall_rejected;
    memcpy(wallExpected,fb,sizeof(wallExpected));
    wallDisabled=1;
    wall_render_frame(fb,env);
    ++dos_wall_verify_frames;
    if(memcmp(wallExpected,fb,sizeof(wallExpected))) ++dos_wall_verify_failures;
    dos_wall_sources=sources; dos_wall_tests=tests; dos_wall_rejected=rejected;
    wallDisabled=0;
}
'''
        recipe.append(dict(operation='replace',before=text,after=text+tail,count=1))
        text+=tail
    return text.encode('utf-8'),recipe
