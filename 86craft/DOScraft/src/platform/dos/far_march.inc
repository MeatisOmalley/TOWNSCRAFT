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
        int px=(g_cam.x<<8)+((dx*start)>>6),pz=(g_cam.z<<8)+((dz*start)>>6);
        int end=farDepthEnd[x+width/2],detail=farDepthDetail[x+width/2];
        while(depth<=end && top>0) {
#else
        int dx=((sy+((cy*u)>>14))*16384)/norm;
        int dz=((cy-((sy*u)>>14))*16384)/norm;
        int px=(g_cam.x<<8)+g_viewDist*(dx*4),pz=(g_cam.z<<8)+g_viewDist*(dz*4);
        int d=g_viewDist;
        while(d<=limit && top>0) {
#endif
            u8 b;
            int wx=px>>16,wz=pz>>16;
#ifdef DOS_FAR_VIEW_DEPTH
            int h=depth<=detail ? far_boundary_sample(wx,wz,&b) : far_lookup(wx,wz,&b);
            int reciprocal=first ? firstInv : inv[d];
            int fog=MIN(8,((depth*norm)>>22)/16);
#else
            int h=d<=g_viewDist+4 ? far_boundary_sample(wx,wz,&b) : far_lookup(wx,wz,&b);
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
                    u8 material=far_material(wx,wz,b),color=farShade[fog][material],other=color;
#ifdef DOS_FAR_DETAILS
                    if(level>=2 && b!=B_WATER && b!=B_GLASS)
                        other=(color&240)|MIN(15,(color&15)+1);
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
                        far_fill_column(&column,y,color,other,material>>7);
#if FAR_CAPTURE
                        {
                            int n=farBandCount[cached];
                            if(n<32) {
                                FarBandRun *r=&farBandRuns[cached][n];
                                r->y=y; r->material=material; r->fog=fog; farBandCount[cached]=n+1;
                            } else cacheGood=0;
                        }
#endif
                    }
                    FAR_COUNT(dos_far_strips); top=y;
                }
            }
#ifdef DOS_FAR_VIEW_DEPTH
            if(first) {
                first=0; d=(start>>8)+1;
                px=(g_cam.x<<8)+dx*d*4; pz=(g_cam.z<<8)+dz*d*4;
            } else {
                int step=d<16 ? 1 : 2;
                d+=step; px+=dx*4*step; pz+=dz*4*step;
            }
            depth=d*256;
#else
            int step=d<16 ? 1 : 2;
            d+=step; px+=dx*4*step; pz+=dz*4*step;
#endif
        }
        skyline[x]=top;
    }
    return cacheGood;
}
