/* Investigation fixture only: complete house, no edits, AI or streaming travel.
   Deliberately scripted cameras can intersect collision geometry. */
extern u32 stdTerrainHash;
u32 stdDone,stdInfo[8],stdStats[6][12],stdHist[6][256],stdEdits[32][7],stdEditCount;
u32 auditUpdate,auditRender;
static u32 houseLast,houseStart;
static int housePhase,houseSample=-1;
static u32 std_us(void)
{
    static u32 oldTick,oldLow,total;
    u32 tick=g_ticks; u16 low=inw(0x26);
    u32 delta=(u16)(low-(u16)oldLow);
    int estimate=(int)(tick-oldTick)*10000-(int)delta;
    total+=delta+(estimate>=0 ? (u32)((estimate+32768)>>16)<<16 : 0);
    oldTick=tick; oldLow=low; return total;
}
static void audit_tick(void)
{
    if(GS_TITLE==state)
    {
        new_game(4242); mobs_clear(); state=GS_PLAY;
        g_renderScale=2; g_viewDist=12; g_flatDist=0; g_distanceMode=0;
        g_occlusion=HOUSE_OCC; STD_INTERLACE;
        g_time=6000; g_skyDarken=0;
        extern int g_profEnable; extern u32 g_profCount;
        g_profEnable=0; g_profCount=0;
        stdInfo[0]=2; stdInfo[1]=g_W; stdInfo[2]=stdTerrainHash;
        stdInfo[3]=g_renderScale; stdInfo[4]=g_viewDist;
        houseLast=houseStart=std_us();
    }
    if(GS_PLAY!=state || stdDone) return;
    u32 now=std_us(),elapsed=now-houseLast;
    if(houseSample>=0)
    {
        u32 *s=stdStats[houseSample];
        ++s[0]; s[1]+=elapsed; s[2]=MAX(s[2],elapsed);
        s[3]+=auditUpdate; s[4]=MAX(s[4],auditUpdate);
        s[5]+=auditRender; s[6]+=g_statFaces; s[7]+=g_statItems;
        s[8]+=elapsed>66667; s[9]+=elapsed>100000;
        /* Reuse the unused edit count slot for textured span samples. */
        s[10]+=g_statPixels;
        ++stdHist[houseSample][MIN(elapsed/2000,255)];
    }
    houseLast=now; auditUpdate=auditRender=0; g_statPixels=0;
    u32 t=now-houseStart;
    if(t>=10000000u)
    {
        if(++housePhase>=4) { stdDone=1; return; }
        houseStart=now; t=0;
    }
    houseSample=t>=2000000u ? housePhase : -1;
    u32 active=t<2000000u ? 0 : t-2000000u;
    /* outside-static, inside-static, inside moving, inside looking up */
    int move=housePhase==2 ? (int)((active/20000u)%256) : 0;
    g_player.body.x=40*FU+FU/2+move*16;
    g_player.body.z=(housePhase==0 ? 37 : 49)*FU+FU/2;
    g_player.body.y=24*FU;
    g_player.body.vx=g_player.body.vy=g_player.body.vz=0;
    g_player.health=MAX_HEALTH; g_player.dead=0;
    g_player.yaw=housePhase==2 ? (active/40000u)%64 : 0;
    g_player.pitch=housePhase==3 ? 200 : -40;
}
