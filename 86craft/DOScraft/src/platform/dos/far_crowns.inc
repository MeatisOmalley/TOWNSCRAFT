/* Crown projections use 1 KiB of the already reserved CRT stack. There is no
 * permanent tree list, second surface map, depth buffer or new heap allocation.
 * Existing 4-byte band runs also carry their six-bit depth for ground clipping. */
typedef struct { u16 x,z,depth; u8 h,crown; } FarCrown;
static void far_crown_piece(const FarCrown *tree,int center,int reciprocal,int vertical,
    int width,int horizon,int upper,int lower,int radius,int fog,int side)
{
    int y0=CLAMP(horizon-(((upper*256-g_cam.y)*vertical)>>16),0,vh);
    int y1=CLAMP(horizon-(((lower*256-g_cam.y)*vertical)>>16),0,vh);
    int reach=(radius*reciprocal)>>16,left=MAX(0,center-reach),right=MIN(vw,center+reach+1);
    if(y1<=y0 || right<=left) return;
    u16 ink=far_ink(B_LEAVES,fog,side);int depth=tree->depth>>8;
    for(int x=(left/width)*width;x<right;x+=width) {
        if(x+width>vw) break;
        int ray=x/width,bottom=y1;
        for(int i=0;i<farBandCount[ray];++i) {
            const FarBandRun *r=&farBandRuns[ray][i];
            if(depth<=(r->fog&63)+2) break;
            bottom=MIN(bottom,r->y);
        }
        if(bottom>y0) {
            FarColumn column;raster_far_column(&column,x,width,bottom);far_paint_ink(&column,y0,ink);
        }
    }
}
static void far_draw_crowns(int width,int level,int horizon,int gain,int f)
{
    if(!dos_far_caps) return;
    FarCrown trees[128];int count=0,limit=farBandLimit[level];
    int px=g_cam.x>>8,pz=g_cam.z>>8,sy=fsin(g_cam.yaw),cy=fcos(g_cam.yaw);
    for(int z=MAX(0,(pz-limit)>>1);z<=MIN(511,(pz+limit)>>1);++z)
    for(int x=MAX(0,(px-limit)>>1);x<=MIN(511,(px+limit)>>1);++x) {
        int encoded=dos_far_map[(z*512+x)*2],crown=encoded>>6;
        if(!crown) continue;
        int tx=x*2+1,tz=z*2+1,bx=tx-px,bz=tz-pz,r2=bx*bx+bz*bz;
        if(r2<g_viewDist*g_viewDist || r2>limit*limit) continue;
        int dx=tx*256-g_cam.x,dz=tz*256-g_cam.z,depth=(dx*sy+dz*cy)>>14;
        if(depth<=0) continue;
        int right=(dx*cy-dz*sy)>>14;
        if(ABS(right)>(((depth*farSide[vw-1])>>14)+640)) continue;
        FarCrown tree={tx,tz,depth,encoded&63,crown};
        if(count==128) {
            if(depth>=trees[0].depth) continue;
            --count;for(int i=0;i<count;++i) trees[i]=trees[i+1];
        }
        int at=count++;while(at>0 && trees[at-1].depth<depth) { trees[at]=trees[at-1];--at; }
        trees[at]=tree;
    }
    for(int i=0;i<count;++i) {
        const FarCrown *tree=&trees[i];int dx=tree->x*256-g_cam.x,dz=tree->z*256-g_cam.z;
        int right=(dx*cy-dz*sy)>>14,center=vw/2+(right*f)/tree->depth;
        int reciprocal=(f<<16)/tree->depth,vertical=(reciprocal*gain)>>14;
        int bx=tree->x-px,bz=tree->z-pz,fog=MIN(8,isqrt(bx*bx+bz*bz)/16);
        far_crown_piece(tree,center,reciprocal,vertical,width,horizon,tree->h+tree->crown,tree->h,384,fog,0);
        far_crown_piece(tree,center,reciprocal,vertical,width,horizon,tree->h,tree->h-2,640,fog,1);
    }
}
