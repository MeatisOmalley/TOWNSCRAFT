/* B8: cached cylindrical horizon, then a live heightmap band, then the
 * unchanged polygon world. No disk access or new near-face collection. */
#define FAR_PAN_COLS 256
#define FAR_PAN_ROWS 128
#define FAR_PAN_RUNS 32
typedef struct { u8 a,b,block,fog; } FarRun;
static FarRun farPan[FAR_PAN_COLS][FAR_PAN_RUNS];
static u8 farPanCount[FAR_PAN_COLS];
static int farPanX,farPanY,farPanZ,farPanNext=FAR_PAN_COLS,farPanValid;
static u32 farPanVersion;
static int farSide[SCR_W],farNorm[SCR_W],farAngle[SCR_W],farRayWidth;
static u8 farShade[9][256];
static int farShadeDarken=-1,farPanLevel;
static int farShadeDetail=-1;
static const u8 *farReady;
typedef struct { u8 y,material,fog; } FarBandRun;
static FarBandRun farBandRuns[SCR_W/2][32];
static u8 farBandCount[SCR_W/2];
static u8 farBandStart[SCR_W/2];
static Camera farBandCamera;
static int farBandValid,farBandView,farBandScale,farBandLevel;
static u32 farBandVersion,farBandBoundary,farBandResident;
static u32 far_resident_signature(void)
{
    u32 signature=0;
    if(farReady) for(int s=0;s<g_residentColumns;++s) {
        u32 owner=(u16)g_columnCoords[s*2]|((u32)(u16)g_columnCoords[s*2+1]<<16);
        signature=(signature^owner^farReady[s])*16777619u;
    }
    return signature;
}
static const int farBandLimit[4]={0,24,32,40},farHorizonLimit[4]={0,64,96,128};
#ifdef DOS_FAR_VIEW_DEPTH
static int farDepthStart[SCR_W],farDepthEnd[SCR_W],farDepthDetail[SCR_W],farDepthInv[SCR_W];
static int farDepthWidth,farDepthView,farDepthLevel;
static void far_depth_setup(int level,int f)
{
    if(farDepthWidth==vw && farDepthView==g_viewDist && farDepthLevel==level) return;
    farDepthWidth=vw; farDepthView=g_viewDist; farDepthLevel=level;
    for(int x=0;x<vw;++x) {
        int n=farNorm[x];
        farDepthStart[x]=(g_viewDist*256*16384)/n;
        farDepthEnd[x]=(farBandLimit[level]*256*16384)/n;
        farDepthDetail[x]=((g_viewDist+4)*256*16384)/n;
        farDepthInv[x]=(f<<16)/farDepthStart[x];
    }
}
#endif
static inline int far_lookup(int x,int z,u8 *b);
static inline u8 far_material(int x,int z,u8 b)
{
#ifdef DOS_FAR_DETAILS
    if(dos_far_enabled>=2 && b!=B_WATER && b!=B_GLASS &&
       (unsigned)x<1024 && (unsigned)z<1024)
        b|=dos_far_map[((z&~1)<<9)+(x&~1)+1]&224;
#endif
    return b;
}
static inline void far_strip(int x,int width,int y0,int y1,u8 b,int fog)
{
    u8 color=farShade[fog][b];
#ifdef DOS_FAR_DETAILS
    if(dos_far_enabled>=2 && BLK_ID(b)!=B_WATER && BLK_ID(b)!=B_GLASS) {
        u8 other=(color&240)|MIN(15,(color&15)+1);
        raster_far_pattern(x,width,y0,y1,color,other,b>>7);
        return;
    }
#endif
    raster_far_strip(x,width,y0,y1,color);
}
u32 dos_far_samples,dos_far_strips,dos_far_pan_builds,dos_far_frames;
#ifdef DOSCRAFT_BENCH
#define FAR_COUNT(counter) (++counter)
#else
#define FAR_COUNT(counter) ((void)0)
#endif
#ifdef DOS_BENCH_PROFILE_FAR
u32 dos_far_us[3];
#endif

static void far_rays(void)
{
    if(farRayWidth==vw) return;
    farRayWidth=vw;
    for(int x=0;x<vw;++x) {
        int u=((x*16+8-cx16)*16384)/focal16;
        farSide[x]=u; farNorm[x]=isqrt(16384*16384+u*u);
        int lo=0,hi=128;
        while(lo<hi) {
            int a=(lo+hi)/2;
            if(fsin(a)*16384<fcos(a)*ABS(u)) lo=a+1; else hi=a;
        }
        farAngle[x]=u<0 ? -lo : lo;
    }
}
static void far_pan_column(int a)
{
    u16 rows[FAR_PAN_ROWS]={0};
    int sx=fsin(a*4),sz=fcos(a*4),top=FAR_PAN_ROWS;
    for(int d=farBandLimit[farPanLevel];d<=farHorizonLimit[farPanLevel] && top>0;d+=4) {
        u8 b;
        int x=(farPanX+((sx*d*256)>>14))>>8;
        int z=(farPanZ+((sz*d*256)>>14))>>8;
        int h=far_lookup(x,z,&b);
        if(!h) continue;
        int y=CLAMP(64-((h*256-farPanY)*86)/(d*256),0,FAR_PAN_ROWS);
        if(y>=top) continue;
        u16 c=far_material(x,z,b)|((d/16)<<8);
        for(int r=y;r<top;++r) rows[r]=c;
        top=y;
    }
    int n=0;
    for(int y=0;y<FAR_PAN_ROWS;) {
        int end=y+1;
        while(end<FAR_PAN_ROWS && rows[end]==rows[y]) ++end;
        if(rows[y]) {
            if(n==FAR_PAN_RUNS) { farPan[a][n-1].b=end; }
            else {
                FarRun *r=&farPan[a][n++];
                r->a=y; r->b=end; r->block=rows[y]&255; r->fog=rows[y]>>8;
            }
        }
        y=end;
    }
    farPanCount[a]=n; ++dos_far_pan_builds;
}
static u8 far_color(u8 b,int fog,const RenderEnv *env)
{
    int darken=env->skyDarken+fog;
#ifdef DOS_FAR_DETAILS
    if(dos_far_enabled>=2 && BLK_ID(b)!=B_WATER && BLK_ID(b)!=B_GLASS)
        darken+=((b>>5)&3)-1+(b>>7);
#endif
    int light=CLAMP(15-darken,0,15);
    return g_shadeLUT[light][texAvg[g_blockDef[BLK_ID(b)].tex[DIR_PY]]];
}
static inline int far_lookup(int x,int z,u8 *b)
{
    if((unsigned)x>=1024 || (unsigned)z>=1024) return 0;
    const u8 *p=dos_far_map+((z&~1)<<9)+(x&~1);
    *b=BLK_ID(p[1]); return p[0];
}
static inline int far_boundary_sample(int x,int z,u8 *b)
{
    if((unsigned)x>=1024 || (unsigned)z>=1024) return 0;
    if(farReady) {
        int s=g_columnMap[(z>>4)*64+(x>>4)];
        if(s>=0 && farReady[s]) {
            int cell=((z&15)<<4)+(x&15),h=g_height[s*256+cell];
            const u8 *p=g_blocks+s*12288+cell*48;
            while(h && !(blk_flags(p[h-1])&BF_CUBE)) --h;
            *b=h ? BLK_ID(p[h-1]) : B_AIR; return h;
        }
    }
    return far_lookup(x,z,b);
}
#define FAR_MARCH_NAME far_march_plain
#define FAR_CAPTURE 0
#include "far_march.inc"
#undef FAR_CAPTURE
#undef FAR_MARCH_NAME
#define FAR_MARCH_NAME far_march_capture
#define FAR_CAPTURE 1
#include "far_march.inc"
#undef FAR_CAPTURE
#undef FAR_MARCH_NAME

static void far_replay_band(int width,int level,u8 *skyline)
{
    for(int x=0;x<vw;x+=width) {
        int cached=x/width;
        FarColumn column; raster_far_column(&column,x,width,farBandStart[cached]);
        for(int i=0;i<farBandCount[cached];++i) {
            const FarBandRun *r=&farBandRuns[cached][i];
            u8 color=farShade[r->fog][r->material],other=color;
#ifdef DOS_FAR_DETAILS
            if(level>=2 && BLK_ID(r->material)!=B_WATER && BLK_ID(r->material)!=B_GLASS)
                other=(color&240)|MIN(15,(color&15)+1);
#endif
            far_fill_column(&column,r->y,color,other,r->material>>7);
            FAR_COUNT(dos_far_strips);
        }
        skyline[x]=column.top;
    }
}

static void draw_far_field(const RenderEnv *env)
{
    if(!dos_far_enabled || !dos_far_map || env->underground || env->underwater) return;
    /* Heightmap/cylindrical projection is for shallow pitch. Full near-world
     * pitch stays untouched; do not shear a distant landscape into the sky. */
    if(ABS(g_cam.pitch)>80) return;
    ++dos_far_frames;
#ifdef DOS_BENCH_PROFILE_FAR
    u32 farStart=bench_us(),farMark;
#endif
    int sp=fsin(g_cam.pitch),cp=fcos(g_cam.pitch),f=focal16>>4;
    int horizon=vh/2+(f*sp)/cp;
    int gain=(16384*16384)/(cp*cp>>14);
    if(farShadeDarken!=env->skyDarken || farShadeDetail!=(dos_far_enabled>=2)) {
        for(int fog=0;fog<9;++fog) for(int style=0;style<8;++style)
        for(int b=0;b<NUM_BLOCKS;++b) farShade[fog][b|(style<<5)]=far_color(b|(style<<5),fog,env);
        farShadeDarken=env->skyDarken;
        farShadeDetail=dos_far_enabled>=2;
    }
    far_rays();
    int level=dos_far_enabled,move=(level==1 ? 8 : 4)*256;
    if(!farPanValid || ABS(g_cam.x-farPanX)>=move ||
       ABS(g_cam.z-farPanZ)>=move || ABS(g_cam.y-farPanY)>=256 || farPanLevel!=level ||
       farPanVersion!=dos_far_version) {
        farPanX=g_cam.x; farPanY=g_cam.y; farPanZ=g_cam.z;
        farPanVersion=dos_far_version; farPanNext=0;
        int newLevel=farPanLevel!=level; farPanLevel=level;
        if(!farPanValid || newLevel) {
            for(int a=0;a<FAR_PAN_COLS;++a) far_pan_column(a);
            farPanNext=FAR_PAN_COLS; farPanValid=1;
        }
    }
    for(int n=0;n<(level==1 ? 4 : level==2 ? 8 : 12) && farPanNext<FAR_PAN_COLS;++n) far_pan_column(farPanNext++);
#ifdef DOS_BENCH_PROFILE_FAR
    farMark=bench_us(); dos_far_us[0]+=farMark-farStart; farStart=farMark;
#endif
    /* Draw the live band first and retain each column's skyline. Panorama
     * pixels below that skyline were previously drawn and then overwritten. */
    int width=(level==1 ? 8 : level==2 ? 4 : 2)/g_renderScale;
    farReady=dos_far_ready_flags();
    u32 resident=far_resident_signature();
    int samePose=farBandCamera.x==g_cam.x && farBandCamera.y==g_cam.y &&
        farBandCamera.z==g_cam.z && farBandCamera.yaw==g_cam.yaw && farBandCamera.pitch==g_cam.pitch &&
        farBandView==g_viewDist && farBandScale==g_renderScale && farBandLevel==level &&
        farBandVersion==dos_far_version && farBandBoundary==dos_far_boundary_version && farBandResident==resident;
    int reuse=farBandValid && samePose,capture=!reuse && samePose;
    int cacheGood=1;
    u8 skyline[SCR_W];
    /* One radial reciprocal per depth, shared by all screen columns. */
    int inv[41];
    int limit=farBandLimit[level];
    for(int d=1;d<=limit;++d) {
        inv[d]=(f<<16)/(d*256);
#ifdef DOS_FAR_VIEW_DEPTH
        inv[d]=(inv[d]*gain)>>14;
#endif
    }
#ifdef DOS_FAR_VIEW_DEPTH
    far_depth_setup(level,f);
#endif
    if(reuse) far_replay_band(width,level,skyline);
    else if(capture) cacheGood=far_march_capture(width,level,horizon,gain,inv,skyline);
    else cacheGood=far_march_plain(width,level,horizon,gain,inv,skyline);
    farBandValid=cacheGood && (reuse || capture); farBandCamera=g_cam;
    farBandView=g_viewDist; farBandScale=g_renderScale; farBandLevel=level;
    farBandVersion=dos_far_version; farBandBoundary=dos_far_boundary_version; farBandResident=resident;
#ifdef DOS_BENCH_PROFILE_FAR
    farMark=bench_us(); dos_far_us[2]+=farMark-farStart; farStart=farMark;
#endif
    /* The exposed part is exactly the same panorama as before; near world
     * polygons still draw over both distant layers afterward. */
    int panRows[FAR_PAN_ROWS+1];
    for(int r=0;r<=FAR_PAN_ROWS;++r)
        panRows[r]=horizon-((((64-r)*f)/86)*gain>>14);
    for(int x=0;x<vw;) {
        int top=skyline[x];
        if(!top) { x+=width; continue; }
        int a=((g_cam.yaw+farAngle[x+width/2])&ANG_MASK)>>2;
        int group=width;
        for(int candidate=width*2;candidate<=8/g_renderScale;candidate*=2) {
            if((x&(candidate-1)) || x+candidate>vw) break;
            int match=1;
            for(int xx=x+width;xx<x+candidate;xx+=width)
                if(skyline[xx]!=top || (((g_cam.yaw+farAngle[xx+width/2])&ANG_MASK)>>2)!=a) { match=0; break; }
            if(!match) break;
            group=candidate;
        }
        for(int i=0;i<farPanCount[a];++i) {
            const FarRun *r=&farPan[a][i];
            int y0=MAX(0,panRows[r->a]);
            if(y0>=top) break;
            int y1=MIN(top,panRows[r->b]);
            if(y1<=y0) continue;
            far_strip(x,group,y0,y1,r->block,r->fog);
            FAR_COUNT(dos_far_strips);
        }
        x+=group;
    }
#ifdef DOS_BENCH_PROFILE_FAR
    dos_far_us[1]+=bench_us()-farStart;
#endif
}
