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
static u8 farShade[9][NUM_BLOCKS];
static int farShadeDarken=-1,farPanLevel;
static const int farBandLimit[4]={0,24,32,40},farHorizonLimit[4]={0,64,96,128};
static inline int far_lookup(int x,int z,u8 *b);
u32 dos_far_samples,dos_far_strips,dos_far_pan_builds;
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
        u16 c=b|((d/16)<<8);
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
    int light=CLAMP(15-env->skyDarken-fog,0,15);
    return g_shadeLUT[light][texAvg[g_blockDef[b].tex[DIR_PY]]];
}
static inline int far_lookup(int x,int z,u8 *b)
{
    if((unsigned)x>=1024 || (unsigned)z>=1024) return 0;
    const u8 *p=dos_far_map+((z&~1)<<9)+(x&~1);
    *b=p[1]; return p[0];
}
static void draw_far_field(const RenderEnv *env)
{
    if(!dos_far_enabled || !dos_far_map || env->underground || env->underwater) return;
    /* Heightmap/cylindrical projection is for shallow pitch. Full near-world
     * pitch stays untouched; do not shear a distant landscape into the sky. */
    if(ABS(g_cam.pitch)>80) return;
#ifdef DOS_BENCH_PROFILE_FAR
    u32 farStart=bench_us(),farMark;
#endif
    int sp=fsin(g_cam.pitch),cp=fcos(g_cam.pitch),f=focal16>>4;
    int horizon=vh/2+(f*sp)/cp;
    int gain=(16384*16384)/(cp*cp>>14);
    if(farShadeDarken!=env->skyDarken) {
        for(int fog=0;fog<9;++fog) for(int b=0;b<NUM_BLOCKS;++b)
            farShade[fog][b]=far_color(b,fog,env);
        farShadeDarken=env->skyDarken;
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
    /* Panorama is farther than every live-band sample. Store material rather
     * than shaded pixels so sun/day/night changes require no panorama rebuild. */
    int width=(level==1 ? 8 : level==2 ? 4 : 2)/g_renderScale;
    for(int x=0;x<vw;x+=width) {
        int a=((g_cam.yaw+farAngle[x+width/2])&ANG_MASK)>>2;
        for(int i=0;i<farPanCount[a];++i) {
            const FarRun *r=&farPan[a][i];
            int y0=horizon-((((64-r->a)*f)/86)*gain>>14);
            int y1=horizon-((((64-r->b)*f)/86)*gain>>14);
            raster_far_strip(x,width,y0,y1,farShade[r->fog][r->block]);
            ++dos_far_strips;
        }
    }
#ifdef DOS_BENCH_PROFILE_FAR
    farMark=bench_us(); dos_far_us[1]+=farMark-farStart; farStart=farMark;
#endif
    /* One radial reciprocal per depth, shared by all screen columns. */
    int inv[41];
    int limit=farBandLimit[level];
    for(int d=g_viewDist;d<=limit;++d) inv[d]=(f<<16)/(d*256);
    int sy=fsin(g_cam.yaw),cy=fcos(g_cam.yaw);
    for(int x=0;x<vw;x+=width) {
        int norm=farNorm[x+width/2],u=farSide[x+width/2],top=vh;
        int dx=((sy+((cy*u)>>14))*16384)/norm;
        int dz=((cy-((sy*u)>>14))*16384)/norm;
        int px=(g_cam.x<<8)+g_viewDist*(dx*4),pz=(g_cam.z<<8)+g_viewDist*(dz*4);
        for(int d=g_viewDist;d<=limit && top>0;d+=d<16 ? 1 : 2) {
            u8 b;
            int wx=px>>16,wz=pz>>16,step=d<16 ? 1 : 2;
            px+=dx*4*step; pz+=dz*4*step;
            int h=d<=g_viewDist+4 ? dos_far_sample(wx,wz,1,&b) : far_lookup(wx,wz,&b);
            ++dos_far_samples;
            if(!h) continue;
            int elevation=((h*256-g_cam.y)*inv[d])>>16;
            elevation=(elevation*norm)>>14;
            int y=CLAMP(horizon-((elevation*gain)>>14),0,vh);
            if(y>=top) continue;
            raster_far_strip(x,width,y,top,farShade[d/16][b]);
            ++dos_far_strips; top=y;
        }
    }
#ifdef DOS_BENCH_PROFILE_FAR
    dos_far_us[2]+=bench_us()-farStart;
#endif
}
