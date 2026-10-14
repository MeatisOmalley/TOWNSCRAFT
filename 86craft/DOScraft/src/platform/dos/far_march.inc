#ifndef FAR_LOOKUP
/* Single-addressing includers name the samplers directly. */
#define FAR_LOOKUP far_lookup
#define FAR_MATERIAL far_material
#define FAR_BOUNDARY far_boundary_sample
#define FAR_MARCH_DEFAULT_SAMPLERS
#endif
/* Compile separate plain/capture loops so moving frames never pay for
 * recording an immediately obsolete strip list. Included twice. */
static int FAR_MARCH_NAME(int width,int level,int horizon,int gain,const int *inv,u8 *skyline)
{
    int cacheGood=1;
    int sy=fsin(g_cam.yaw),cy=fcos(g_cam.yaw),limit=farBandLimit[level];
    for(int x=0;x<vw;x+=width) {
        int norm=farNorm[x+width/2],u=farSide[x+width/2],top=vh;
        FarColumn column; raster_far_column(&column,x,width,vh);
#if FAR_CAPTURE
        int cached=x/width;
        farBandCount[cached]=0; farBandStart[cached]=vh;
#endif
#ifdef DOS_FAR_SKIP_FIRST
        int firstFill=1;
#endif
#ifdef DOS_FAR_VIEW_DEPTH
        int dx=sy+((cy*u)>>14),dz=cy-((sy*u)>>14);
        int start=farDepthStart[x+width/2],depth=start,d=start>>8,first=1;
        int firstInv=(farDepthInv[x+width/2]*gain)>>14;
        int previous=inv[MAX(1,(start>>8)-1)];
        int px=(g_cam.x<<8)+((dx*start)>>6),pz=(g_cam.z<<8)+((dz*start)>>6);
        int end=farDepthEnd[x+width/2],detail=farDepthDetail[x+width/2];
        while(depth<=end && top>0) {
#else
        int dx=((sy+((cy*u)>>14))*16384)/norm;
        int dz=((cy-((sy*u)>>14))*16384)/norm;
        int px=(g_cam.x<<8)+g_viewDist*(dx*4),pz=(g_cam.z<<8)+g_viewDist*(dz*4);
        int d=g_viewDist;
        int previous=(((inv[MAX(1,d-1)]*norm)>>14)*gain)>>14;
        while(d<=limit && top>0) {
#endif
            u8 b;
            int wx=px>>16,wz=pz>>16;
#ifdef DOS_FAR_VIEW_DEPTH
            int h=depth<=detail ? FAR_BOUNDARY(wx,wz,&b) : FAR_LOOKUP(wx,wz,&b);
            int reciprocal=first ? firstInv : inv[d];
            int fog=MIN(8,((depth*norm)>>22)/16);
#else
            int h=d<=g_viewDist+4 ? FAR_BOUNDARY(wx,wz,&b) : FAR_LOOKUP(wx,wz,&b);
            int reciprocal=inv[d],fog=d/16;
#endif
            FAR_COUNT(dos_far_samples);
            if(h) {
                int elevation=((h*256-g_cam.y)*reciprocal)>>16;
#ifdef DOS_FAR_VIEW_DEPTH
                int y=CLAMP(horizon-elevation,0,vh);
#else
                elevation=(elevation*norm)>>14;
                int y=CLAMP(horizon-((elevation*gain)>>14),0,vh);
#endif
                if(y<top) {
                    u8 material=FAR_MATERIAL(wx,wz,b);
                    int cap=top;
#ifdef DOS_FAR_DETAILS
                    if(level>=DOS_FAR_VERY_LOW) {
                        /* Project this sample's top at the previous depth.
                         * Horizontal ground gets a full cap; a rise exposes
                         * a side below it. This is a heightmap approximation. */
                        cap=CLAMP(horizon-(((h*256-g_cam.y)*previous)>>16),y,top);
                        if(cap==y) cap=MIN(top,y+1);
                    }
#endif
#ifdef DOS_FAR_SKIP_FIRST
                    if(firstFill) {
                        firstFill=0; raster_far_column(&column,x,width,y);
#if FAR_CAPTURE
                        farBandStart[cached]=y;
#endif
                    } else
#endif
                    {
                        far_paint_surface(&column,y,cap,material,fog);
#if FAR_CAPTURE
                        {
                            int n=farBandCount[cached];
                            if(n<32) {
                                FarBandRun *r=&farBandRuns[cached][n];
                                r->y=y; r->cap=cap; r->material=material; r->fog=fog; farBandCount[cached]=n+1;
                            } else cacheGood=0;
                        }
#endif
                    }
                    FAR_COUNT(dos_far_strips); top=y;
                }
            }
#ifdef DOS_FAR_VIEW_DEPTH
            previous=reciprocal;
            if(first) {
                first=0; d=(start>>8)+1;
                px=(g_cam.x<<8)+dx*d*4; pz=(g_cam.z<<8)+dz*d*4;
            } else {
                int step=d<16 ? 1 : 2;
                d+=step; px+=dx*4*step; pz+=dz*4*step;
            }
            depth=d*256;
#else
            previous=(((reciprocal*norm)>>14)*gain)>>14;
            int step=d<16 ? 1 : 2;
            d+=step; px+=dx*4*step; pz+=dz*4*step;
#endif
        }
        skyline[x]=top;
    }
    return cacheGood;
}
#ifdef FAR_MARCH_DEFAULT_SAMPLERS
#undef FAR_LOOKUP
#undef FAR_MATERIAL
#undef FAR_BOUNDARY
#undef FAR_MARCH_DEFAULT_SAMPLERS
#endif
