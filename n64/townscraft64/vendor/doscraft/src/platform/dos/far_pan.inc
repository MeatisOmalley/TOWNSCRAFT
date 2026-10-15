/* Distant panorama column builder; instantiated per map addressing in
 * far_render.inc (FAR_PAN_NAME, FAR_LOOKUP, FAR_MATERIAL). */
static void FAR_PAN_NAME(int a)
{
#ifdef DOS_FAR_REPROJECT
    int sx=fsin(a*4),sz=fcos(a*4),n=0;
    for(int d=farBandLimit[farPanLevel];d<=farHorizonLimit[farPanLevel];d+=4) {
        u8 b;
        int x=(farPanX+((sx*d*256)>>14))>>8;
        int z=(farPanZ+((sz*d*256)>>14))>>8;
        int h=FAR_LOOKUP(x,z,&b);
        if(h) {
            FarRun *r=&farPan[a][n++];
            r->h=h; r->depth=d; r->block=FAR_MATERIAL(x,z,b); r->fog=d/16;
        }
    }
    farPanCount[a]=n; ++dos_far_pan_builds;
    farPanStamp[a]=0;
#else
    u16 rows[FAR_PAN_ROWS]={0};
    int sx=fsin(a*4),sz=fcos(a*4),top=FAR_PAN_ROWS;
    for(int d=farBandLimit[farPanLevel];d<=farHorizonLimit[farPanLevel] && top>0;d+=4) {
        u8 b;
        int x=(farPanX+((sx*d*256)>>14))>>8;
        int z=(farPanZ+((sz*d*256)>>14))>>8;
        int h=FAR_LOOKUP(x,z,&b);
        if(!h) continue;
        int y=CLAMP(64-((h*256-farPanY)*86)/(d*256),0,FAR_PAN_ROWS);
        if(y>=top) continue;
        u16 c=FAR_MATERIAL(x,z,b)|((d/16)<<8);
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
#endif
}
