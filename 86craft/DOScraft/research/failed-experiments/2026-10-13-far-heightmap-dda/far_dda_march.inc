/* Included twice, with the same plain/capture split as the fixed-depth march. */
static inline void FAR_DDA_EMIT_NAME(FarColumn *column,int horizon,int h,
    int nearInv,int farInv,u8 material,int fog,int lastHeight,int firstCell,int cached,int *cacheGood)
{
    int delta=h*256-g_cam.y;
    int nearY=CLAMP(horizon-((delta*nearInv)>>16),0,vh);
    int farY=CLAMP(horizon-((delta*farInv)>>16),0,vh);
    int y=MIN(nearY,farY);
    if(y>=column->top) return;
    int cap=CLAMP(nearY,y,column->top);
    /* Keep grass rims on actual rises; do not invent a moving one-row lip
     * at the near cut when a flat cell interval has zero projected height. */
    if(cap==y && (delta>=0 || (!firstCell && h>lastHeight))) cap=MIN(column->top,y+1);
    far_paint_surface(column,y,cap,material,fog);
#if FAR_CAPTURE
    int n=farBandCount[cached];
    if(n<FAR_BAND_RUNS) {
        FarBandRun *r=&farBandRuns[cached][n];
        r->y=y; r->cap=cap; r->material=material; r->fog=fog;
        farBandCount[cached]=n+1;
    } else *cacheGood=0;
#endif
    FAR_COUNT(dos_far_strips);
}
static int FAR_MARCH_NAME(int width,int level,int horizon,int gain,const int *inv,u8 *skyline)
{
    int cacheGood=1;
    far_dda_directions(width);
    for(int x=0;x<vw;x+=width) {
        int ray=x+width/2,norm=farNorm[ray],cached=x/width;
        FarColumn column; raster_far_column(&column,x,width,vh);
#if FAR_CAPTURE
        farBandCount[cached]=0; farBandStart[cached]=vh;
#endif
        FarDda grid;
        grid.dx=farDdaDx[ray]; grid.dz=farDdaDz[ray];
        grid.rx=farDdaRx[ray]; grid.rz=farDdaRz[ray];
        grid.t=(u32)farDepthStart[ray]<<4;
        grid.end=(u32)farDepthEnd[ray]<<4; grid.detail=(u32)farDepthDetail[ray]<<4;
        if(grid.t>=grid.end) { skyline[x]=vh; continue; }
        far_dda_grid(&grid,g_cam.x,g_cam.z,8);
        int lastHeight=0,firstCell=1,runHeight=-1,runFog=0,runKey=0;
        u8 runMaterial=0;
        u32 runStart=grid.t,runEnd=grid.t;
        while(grid.t<grid.end && column.top>0) {
            u32 end=far_dda_exit(&grid);
            if(end<=grid.t) { far_dda_advance(&grid,end,g_cam.x,g_cam.z); continue; }
            u8 block=B_AIR;
            int step=1<<(grid.shift-8),wx=grid.x*step,wz=grid.z*step;
            int h=grid.shift==8 ? far_boundary_sample(wx,wz,&block) : far_lookup(wx,wz,&block);
            FAR_COUNT(dos_far_samples);
            u8 material=h ? far_material(wx,wz,block) : B_AIR;
            int fog=MIN(8,((u32)((grid.t+end)/2)*(u32)norm)>>30);
            /* Bit 5 does not affect the current gentle shade. Quiet materials
             * also ignore the pattern-phase bit; leaves/wood retain it. */
            int mask=223;
            if(block==B_GRASS || block==B_DIRT || block==B_SAND || block==B_WOOL || block==B_WATER || block==B_GLASS) mask&=127;
            if(block==B_WATER || block==B_GLASS) mask=31;
            int key=material&mask;
            if(runHeight>=0 && (h!=runHeight || key!=runKey || fog!=runFog)) {
                if(runHeight) FAR_DDA_EMIT_NAME(&column,horizon,runHeight,
                    far_dda_reciprocal(runStart),far_dda_reciprocal(runEnd),runMaterial,
                    runFog,lastHeight,firstCell,cached,&cacheGood);
                lastHeight=runHeight; firstCell=0; runHeight=-1;
                if(!column.top) break;
            }
            if(runHeight<0) {
                runStart=grid.t; runHeight=h; runMaterial=material; runFog=fog; runKey=key;
                int delta=h*256-g_cam.y;
                if(h && delta>=0) {
                    int nearInv=far_dda_reciprocal(runStart);
                    if(horizon-((delta*nearInv)>>16)<=0) {
                        FAR_DDA_EMIT_NAME(&column,horizon,h,nearInv,nearInv,material,
                            fog,lastHeight,firstCell,cached,&cacheGood);
                        break;
                    }
                }
            }
            runEnd=end;
            if(end==grid.end) break;
            far_dda_advance(&grid,end,g_cam.x,g_cam.z);
        }
        if(column.top && runHeight>0) FAR_DDA_EMIT_NAME(&column,horizon,runHeight,
            far_dda_reciprocal(runStart),far_dda_reciprocal(runEnd),runMaterial,
            runFog,lastHeight,firstCell,cached,&cacheGood);
        skyline[x]=column.top;
    }
    return cacheGood;
}
