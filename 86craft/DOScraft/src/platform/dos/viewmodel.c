#include "viewmodel.h"
#include "raster.h"
#include "render.h"
#include "textures.h"
#include "fmath.h"
#ifdef DOSCRAFT_BENCH
#include "bench.h"
u32 dos_viewmodel_us;
#endif

#if defined(DOS_VIEWMODEL) && !defined(DOS_BENCH_NO_VIEWMODEL)
typedef struct { int x,y,z; } VmPoint;
static struct {
    int active,item,light,lastSwing,lastX,lastZ,walking,initialized;
    u32 tick,start,distance;
} vm;
/* One shaded icon, rebuilt on selection/light/art changes. Wrapped addressing
 * supports this ordinary COFF-aligned page with the atlas 256-byte row stride. */
static u8 iconPage[4096];
static int iconId=-1,iconLight=-1,iconScale=-1;
static int matrix[3][3],origin[3];
static const u8 corners[6][4][3]={
    {{0,1,1},{0,1,0},{0,0,0},{0,0,1}},
    {{1,1,0},{1,1,1},{1,0,1},{1,0,0}},
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
    {{0,1,1},{1,1,1},{1,1,0},{0,1,0}},
    {{0,1,0},{1,1,0},{1,0,0},{0,0,0}},
    {{1,1,1},{0,1,1},{0,0,1},{1,0,1}}
};

void dos_viewmodel_update(int active,int item,int light,int swing,int mining,
                         int walking,int x,int z,u32 ticks)
{
    if(!active) { vm.active=0; vm.initialized=0; return; }
    if(!vm.initialized) {
        vm.lastX=x; vm.lastZ=z; vm.start=ticks-30; vm.lastSwing=0;
        vm.initialized=1;
    }
    int distance=ABS(x-vm.lastX)+ABS(z-vm.lastZ);
    if(walking && distance<8192) vm.distance+=distance;
    if(swing>vm.lastSwing || (mining && ticks-vm.start>=30) ||
       (swing>=5 && ticks-vm.start>=30)) vm.start=ticks;
    vm.active=1; vm.item=(unsigned)item<NUM_ITEMS ? item : 0;
    vm.light=CLAMP(light,0,15); vm.tick=ticks; vm.lastSwing=swing;
    vm.lastX=x; vm.lastZ=z; vm.walking=walking;
}

static void pose(int yaw,int pitch,int roll,int x,int y,int z)
{
    int sy=fsin(yaw),cy=fcos(yaw),sp=fsin(pitch),cp=fcos(pitch),sr=fsin(roll),cr=fcos(roll);
    /* Rotation columns: yaw, then pitch, then roll. */
    matrix[0][0]=((cr*cy)>>14)-((sr*((sp*sy)>>14))>>14);
    matrix[1][0]=((sr*cy)>>14)+((cr*((sp*sy)>>14))>>14);
    matrix[2][0]=(-cp*sy)>>14;
    matrix[0][1]=(-sr*cp)>>14; matrix[1][1]=(cr*cp)>>14; matrix[2][1]=sp;
    matrix[0][2]=((cr*sy)>>14)+((sr*((sp*cy)>>14))>>14);
    matrix[1][2]=((sr*sy)>>14)-((cr*((sp*cy)>>14))>>14);
    matrix[2][2]=(cp*cy)>>14;
    origin[0]=x; origin[1]=y; origin[2]=z;
}

static VmPoint transform(int x,int y,int z)
{
    VmPoint p;
    p.x=origin[0]+((matrix[0][0]*x+matrix[0][1]*y+matrix[0][2]*z)>>14);
    p.y=origin[1]+((matrix[1][0]*x+matrix[1][1]*y+matrix[1][2]*z)>>14);
    p.z=origin[2]+((matrix[2][0]*x+matrix[2][1]*y+matrix[2][2]*z)>>14);
    return p;
}

static void polygon(const VmPoint *p,const u8 *tile,int flags,int width,int height)
{
    RVert v[4]; int focal=(width*172/320)*16;
    for(int i=0;i<4;++i) {
        /* The fixed foreground poses stay well beyond the near plane. */
        if(p[i].z<64) return;
        v[i].x=width*8+(p[i].x*focal)/p[i].z;
        v[i].y=height*8-(p[i].y*focal)/p[i].z;
        v[i].u=(i==1 || i==2) ? (16<<16)-512 : 512;
        v[i].v=i>=2 ? (16<<16)-512 : 512;
    }
    if(tile) raster_poly(v,4,tile,flags);
    else raster_flat_poly(v,4,(u8)flags);
    ++g_statFaces;
}

static void box(int halfX,int halfY,int halfZ,const u8 *textures,int opaque,int width,int height)
{
    VmPoint vertex[8]; int face[6],depth[6],count=0;
    for(int i=0;i<8;++i) vertex[i]=transform(i&1 ? halfX : -halfX,
        i&2 ? halfY : -halfY,i&4 ? halfZ : -halfZ);
    for(int d=0;d<6;++d) {
        int axis=d>>1,sign=(d&1) ? 1 : -1;
        const u8 *c=corners[d][0]; const VmPoint *p=&vertex[c[0]|(c[1]<<1)|(c[2]<<2)];
        if(sign*(matrix[0][axis]*p->x+matrix[1][axis]*p->y+matrix[2][axis]*p->z)>=0) continue;
        int k=count++,z=0;
        for(int i=0;i<4;++i) { c=corners[d][i]; z+=vertex[c[0]|(c[1]<<1)|(c[2]<<2)].z; }
        while(k && depth[k-1]<z) { face[k]=face[k-1];depth[k]=depth[k-1];--k; }
        face[k]=d;depth[k]=z;
    }
    for(int f=0;f<count;++f) {
        int d=face[f],texture=textures ? textures[d] : 0,light=MAX(0,vm.light-(d==DIR_PY ? 0 : d==DIR_NZ ? 1 : 2));
        VmPoint p[4]; int transparent=0;
        for(int i=0;i<4;++i) {
            const u8 *c=corners[d][i]; p[i]=vertex[c[0]|(c[1]<<1)|(c[2]<<2)];
        }
        if(!opaque) for(int i=0;i<256;++i) if(!g_tex[texture][i]) { transparent=1;break; }
        if(textures) polygon(p,TEX_TILE(texture,light),transparent ? RP_TRANSPARENT : 0,width,height);
        else polygon(p,0,g_shadeLUT[light][P(R_PEACH,15)],width,height);
    }
}

static const u8 *item_texture(int item)
{
    int icon=g_itemDef[item].icon;
    if(iconId!=icon || iconLight!=vm.light || iconScale!=g_renderScale) {
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
            u8 c=g_tex[icon][y*16+x];
            iconPage[y*256+x]=c ? MAX(1,g_shadeLUT[vm.light][c]) : 0;
        }
        iconId=icon;iconLight=vm.light;iconScale=g_renderScale;
    }
    return iconPage;
}

static void item_plane(int item,int width,int height)
{
    VmPoint p[4];
    p[0]=transform(-96,116,-16);p[1]=transform(96,116,-16);
    p[2]=transform(96,-116,-16);p[3]=transform(-96,-116,-16);
    polygon(p,item_texture(item),RP_TRANSPARENT|RP_WRAP,width,height);
}

void dos_viewmodel_draw(int width,int height)
{
    if(!vm.active) return;
#ifdef DOSCRAFT_BENCH
    u32 stamp=bench_us();
#endif
    u32 age=vm.tick-vm.start;
    int swing=age<30 ? fsin(age*512/30) : 0;
    int phase=(vm.distance>>3)&1023;
    int bobX=vm.walking ? fsin(phase)>>12 : 0;
    int bobY=vm.walking ? ABS(fcos(phase))>>12 : 0;
    int dx=bobX-((swing*36)>>14),dy=bobY+((swing*20)>>14),dz=-((swing*24)>>14);
    int block=vm.item ? g_itemDef[vm.item].placeBlock : 0;
    if(block && (blk_flags(block)&BF_CUBE)) {
        pose(-96,-48-((swing*72)>>14),(swing*80)>>14,128+dx,-98+dy,260+dz);
        box(44,44,44,g_blockDef[block].tex,!!(blk_flags(block)&BF_OPAQUE),width,height);
    } else if(vm.item) {
        /* The item itself fills the foreground; its upper/left edge tilts
         * away from the eye, with the grip extending toward the corner. */
        pose(-64,48-((swing*72)>>14),144+((swing*80)>>14),116+dx,-70+dy,260+dz);
        item_plane(vm.item,width,height);
    } else {
        pose(-56,56-((swing*100)>>14),72+((swing*96)>>14),146+dx,-115+dy,240+dz);
        box(30,82,30,0,1,width,height);
    }
#ifdef DOSCRAFT_BENCH
    dos_viewmodel_us+=bench_us()-stamp;
#endif
}
#else
void dos_viewmodel_update(int active,int item,int light,int swing,int mining,
                         int walking,int x,int z,u32 ticks)
{ (void)active;(void)item;(void)light;(void)swing;(void)mining;(void)walking;(void)x;(void)z;(void)ticks; }
void dos_viewmodel_draw(int width,int height) { (void)width;(void)height; }
#endif
