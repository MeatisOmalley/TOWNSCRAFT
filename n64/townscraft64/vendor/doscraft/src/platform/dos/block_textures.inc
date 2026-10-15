/* Original 16x16 block art. Generation runs at startup/style changes; the palette,
 * transparent masks, shaded atlas layout and render loops are unchanged.
 * Quiet natural surfaces use adjacent shades, grouped into small motifs. */
/* Fine periodic detail, without a coarse lattice or recognizable large motif. */
static int dos_grass_patch(int x,int y)
{
    return hn(x&15,y&15,31)%100;
}
#ifdef DOS_TEXTURE_CHECK
int dos_texture_test_grass_patch(int x,int y)
{
    int saved=seedBase; seedBase=T_GRASS_TOP*131;
    int value=dos_grass_patch(x,y); seedBase=saved; return value;
}
#endif
static void dos_tex_grass_top(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int shade=dos_grass_patch(x,y)<25 ? 13 : 12;
        /* Sparse darker pinpricks; no bright/dark extremes or coarse blobs. */
        if(hn(x,y,32)%100<8) --shade;
        px(x,y,P(R_GRASS,shade));
    }
}
static void dos_tex_dirt(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int detail=hn(x,y,33)%100;
        int shade=detail<28 ? 13 : detail>74 ? 15 : 14;
        px(x,y,P(R_DIRT,shade));
    }
}
static void dos_tex_stone(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int patch=hn(x/3,y/2,35)%100;
        int shade=patch<22 ? 10 : patch>85 ? 12 : 11;
        if(hn(x,y,36)%100<24) shade=11;
        px(x,y,P(R_GRAY,shade));
    }
}
static void dos_tex_grass_side(void)
{
    dos_tex_dirt();
    for(int x=0;x<16;++x) {
        int fringe=3+hn(x/2,0,37)%3;
        for(int y=0;y<fringe;++y)
            px(x,y,P(R_GRASS,12+(hn(x/2,y/2,38)%100<35)));
    }
}
static void dos_tex_leaves(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        px(x,y,P(R_LEAF,hn(x,y,39)%100<34 ? 10 : 11));
    }
    /* Curved leaf outlines and short veins, rather than generic speckles.
     * Keep the dark gaps opaque: they are texture detail, not new cutouts. */
    static const int roots[6][2]={{0,2},{6,0},{11,5},{3,9},{10,12},{14,14}};
    static const int edge[12][2]={{0,2},{1,1},{2,1},{3,0},{4,1},{4,2},
        {5,3},{4,4},{3,4},{2,5},{1,4},{1,3}};
    for(int leaf=0;leaf<6;++leaf) {
        int x=roots[leaf][0],y=roots[leaf][1];
        for(int p=0;p<12;++p) px((x+edge[p][0])&15,(y+edge[p][1])&15,
            P(R_LEAF,(p<4 && (leaf&1)) ? 13 : 12));
        px((x+2)&15,(y+2)&15,P(R_LEAF,12));
        px((x+3)&15,(y+3)&15,P(R_LEAF,11));
    }
}
static void dos_tex_sand(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int grain=hn(x,y,41)%100;
        px(x,y,P(R_SAND,grain<22 ? 14 : 15));
    }
}
static int dos_tex_water_field(int x,int y)
{
    int gx=x>>2,gy=y>>2,fx=(x&3)*64,fy=(y&3)*64;
    int a=hn(gx,gy,60)&255,b=hn((gx+1)&3,gy,60)&255;
    int c=hn(gx,(gy+1)&3,60)&255,d=hn((gx+1)&3,(gy+1)&3,60)&255;
    int top=a+(((b-a)*fx)>>8),bottom=c+(((d-c)*fx)>>8);
    return top+(((bottom-top)*fy)>>8);
}
static void dos_tex_water(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int wave=dos_tex_water_field(x,y),grain=hn(x,y,61)%100;
        int shade=wave<75 ? 10 : wave>185 ? 12 : 11;
        if(grain<3 && shade==11) shade=(grain&1) ? 10 : 12;
        px(x,y,P(R_WATER,shade));
    }
}
static void dos_tex_planks(int ramp,int dark)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int board=y/4,joint=(board*7+3)&15;
        int shade=12-dark;
        if((y&3)==3 || x==joint) shade=9-dark;
        else if(hn(x/3,board,43)%100<35 && (y&3)==1) --shade;
        else if(hn(x/4,board,44)%100<25 && (y&3)==0) ++shade;
        px(x,y,P(ramp,CLAMP(shade,1,15)));
    }
}
static void dos_tex_log_side(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int bend=hn(x/4,y/5,45)%2;
        int grain=(x+bend)&3;
        int shade=grain==0 ? 9 : grain==1 ? 12 : 11;
        if(hn(x/2,y/3,46)%100<18) --shade;
        px(x,y,P(R_BARK,shade));
    }
}
static void dos_tex_log_top(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int d=MAX(ABS(2*x-15),ABS(2*y-15))/2;
        if(d>=7) px(x,y,P(R_BARK,9+hn(x,y,47)%2));
        else px(x,y,P(R_PLANK,12+((d&1)==0)));
    }
}
static void dos_tex_cobble(void)
{
    static const int centers[8][2]={{1,2},{7,1},{12,3},{3,7},{9,7},{14,9},{5,13},{11,14}};
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int best=1000,second=1000,cell=0;
        for(int i=0;i<8;++i) {
            int dx=ABS(x-centers[i][0]),dy=ABS(y-centers[i][1]);
            dx=MIN(dx,16-dx); dy=MIN(dy,16-dy);
            int d=dx*dx+dy*dy;
            if(d<best) { second=best; best=d; cell=i; }
            else if(d<second) second=d;
        }
        /* Restore the accepted pre-grain cobble for the smaller view. */
        int shade=second-best<5 ? 8 : 11+(cell&1)-(best>12);
        px(x,y,P(R_GRAY,shade));
    }
}
static void dos_tex_ore(int ramp,int shade)
{
    /* Grainy stone matrix, with irregular deposits spread across the tile. */
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int grain=hn(x,y,51)%100;
        px(x,y,P(R_GRAY,grain<25 ? 10 : grain>80 ? 12 : 11));
    }
    static const int deposits[8][2]={{1,2},{7,1},{12,3},{4,7},{10,8},{1,12},{7,13},{14,12}};
    static const int shape[6][2]={{0,0},{1,0},{0,1},{1,1},{2,1},{1,2}};
    for(int i=0;i<8;++i) {
        int x=deposits[i][0]+hn(i,0,52)%3-1,y=deposits[i][1]+hn(i,1,52)%3-1;
        for(int p=0;p<6;++p) {
            if(p>2 && hn(i,p,53)%100<35) continue;
            int xx=(x+shape[p][0])&15,yy=(y+shape[p][1])&15;
            int color;
            if(ramp==R_CYAN) {
                color=p==0 ? P(R_GRAY,15) : p==1 ? P(R_SKY,15) : p<4 ? P(R_CYAN,15) : P(R_CYAN,13);
            } else if(ramp==R_FLAME) {
                color=p==0 ? P(R_SAND,15) : p<3 ? P(R_FLAME,15) : p<5 ? P(R_FLAME,13) : P(R_DIRT,14);
            } else if(ramp==R_PEACH) {
                color=P(R_PEACH,p<2 ? 15 : p<4 ? 13 : 11);
            } else {
                color=P(R_GRAY,p<2 ? 3 : p<4 ? 2 : 5);
            }
            px(xx,yy,color);
        }
    }
    (void)shade;
}
static void dos_tex_furnace_fire(void)
{
    rect(3,7,12,7,P(R_GRAY,13)); rect(3,8,12,14,P(R_GRAY,2));
    rect(2,8,2,14,P(R_GRAY,12)); rect(13,8,13,14,P(R_GRAY,10));
    for(int x=4;x<=11;++x) {
        int height=2+hn(x,0,62)%4;
        for(int y=14-height;y<=14;++y)
            px(x,y,P(y>=13 ? R_RED : R_FLAME,y>=13 ? 15 : y==14-height ? 13 : 15));
        if(x&1) px(x,14,P(R_FLAME,15));
    }
    rect(3,15,12,15,P(R_GRAY,11));
}
static void dos_tex_wool(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x)
        px(x,y,P(R_GRAY,hn(x,y,63)%100<15 ? 14 : 15));
}
static void dos_tex_stonebrick(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int mortar=(y&7)==7 || ((x+(y<8 ? 0 : 8))&15)==0;
        int shade=mortar ? 10 : hn(x,y,64)%100<20 ? 12 : 11;
        px(x,y,P(R_GRAY,shade));
    }
}
static void dos_tex_gravel(void)
{
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int patch=hn(x/2,y/2,48)%100;
        int shade=patch<25 ? 10 : patch>70 ? 12 : 11;
        px(x,y,P(patch>91 ? R_DIRT : R_GRAY,shade));
    }
}
static void dos_tex_craft_top(void)
{
    /* A warm inset work surface, nine recessed cells and reinforced corners. */
    dos_tex_planks(R_PLANK,0);
    rect(0,0,15,0,P(R_BARK,6)); rect(0,15,15,15,P(R_BARK,6));
    rect(0,0,0,15,P(R_BARK,6)); rect(15,0,15,15,P(R_BARK,6));
    rect(2,2,13,13,P(R_DIRT,15));
    rect(3,1,12,1,P(R_BARK,10)); rect(3,14,12,14,P(R_BARK,9));
    rect(1,3,1,12,P(R_BARK,10)); rect(14,3,14,12,P(R_BARK,9));
    for(int y=4;y<=10;y+=3) for(int x=4;x<=10;x+=3) {
        rect(x,y,x+1,y+2,P(R_DIRT,12));
        px(x,y,P(R_DIRT,14)); px(x+1,y+2,P(R_BARK,10));
    }
    for(int corner=0;corner<4;++corner) {
        int x=(corner&1) ? 13 : 1,y=(corner&2) ? 13 : 1;
        rect(x,y,x+1,y+1,P(R_PLANK,14));
        px((corner&1)?12:3,(corner&2)?14:1,P(R_PLANK,12));
        px((corner&1)?14:1,(corner&2)?12:3,P(R_PLANK,12));
    }
}
static void dos_tex_craft_side(int front)
{
    dos_tex_planks(R_PLANK,0);
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        int shade=(gp(x,y)&15)+1;
        px(x,y,P(R_PLANK,MIN(15,shade)));
    }
    /* Timber frame, central support and tools hanging from the bench edge. */
    rect(0,0,0,15,P(R_BARK,9)); rect(15,0,15,15,P(R_BARK,9));
    rect(6,0,8,15,P(R_BARK,9)); rect(7,0,7,15,P(R_BARK,10));
    rect(0,0,15,1,P(R_BARK,9)); rect(0,14,15,15,P(R_BARK,8));
    if(front) {
        rect(3,5,4,7,P(R_BARK,7)); rect(3,8,3,11,P(R_GRAY,14));
        rect(2,10,4,11,P(R_GRAY,13));
        rect(11,4,12,6,P(R_BARK,7)); px(12,5,P(R_PLANK,12));
        rect(10,7,12,10,P(R_GRAY,14)); rect(11,11,12,12,P(R_GRAY,13));
        px(10,8,P(R_GRAY,15)); px(10,10,P(R_PLANK,11));
    } else {
        rect(2,5,4,7,P(R_BARK,8)); px(3,6,P(R_PLANK,12));
        rect(4,7,4,12,P(R_GRAY,14)); px(3,10,P(R_GRAY,13));
        rect(11,6,12,7,P(R_BARK,7)); rect(11,8,11,12,P(R_GRAY,14));
        px(10,11,P(R_GRAY,13));
    }
}
