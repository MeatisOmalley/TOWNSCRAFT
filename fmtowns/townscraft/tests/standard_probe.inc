/* Standard benchmark v1, test-only game.c instrumentation.
   2 s settling per phase, then 16 s deterministic motion at 4 blocks/s.
   Aggregates stay small so instrumentation does not reserve a frame log. */
extern u32 stdTerrainHash;
u32 stdDone,stdInfo[8],stdStats[6][12],stdHist[6][256],stdEdits[32][7],stdEditCount;
u32 auditUpdate,auditRender;
static u32 stdLast,stdPhaseStart,stdEditStart;
static int stdPhase=-1,stdLastPhase=-1,stdEditActive,stdEditTargetX,stdEditTargetY,stdEditTargetZ;
static u32 stdEditFrames;
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
		for(int i=0;i<STD_MOBS;++i)
			mob_spawn(MOB_PIG,(24+(i%4)*12)*FU,24*FU,(28+(i/4)*16)*FU);
		g_renderScale=STD_SCALE; g_viewDist=STD_VIEW; g_flatDist=0;
		STD_INTERLACE;
		g_time=6000; g_skyDarken=0;
		extern int g_profEnable; extern u32 g_profCount;
		g_profEnable=0; g_profCount=0;
		stdInfo[0]=1; stdInfo[1]=g_W; stdInfo[2]=stdTerrainHash;
		stdInfo[3]=g_renderScale; stdInfo[4]=g_viewDist;
		stdInfo[5]=STD_MOBS;
		stdPhase=STD_ONLY<0 ? 0 : STD_ONLY;
		stdLast=stdPhaseStart=std_us();
		stdLastPhase=-1;
	}
	if(GS_PLAY!=state || stdDone) return;
	u32 now=std_us(),elapsed=now-stdLast;
	if(stdLastPhase>=0)
	{
		u32 *s=stdStats[stdLastPhase];
		++s[0]; s[1]+=elapsed; s[2]=MAX(s[2],elapsed);
		s[3]+=auditUpdate; s[4]=MAX(s[4],auditUpdate);
		s[5]+=auditRender; s[6]+=g_statFaces; s[7]+=g_statItems;
		s[8]+=elapsed>66667; s[9]+=elapsed>100000;
		++stdHist[stdLastPhase][MIN(elapsed/2000,255)];
	}
	if(stdEditActive)
	{
		++stdEditFrames;
		Chunk *c=&g_chunks[chunk_index(stdEditTargetX/CS,stdEditTargetY/CS,stdEditTargetZ/CS)];
		if(STD_IS_MESHED && !c->dirty)
		{
			u32 *e=stdEdits[stdEditCount-1];
			e[3]=now-stdEditStart; e[4]=stdEditFrames; stdEditActive=0;
		}
	}
	stdLast=now; auditUpdate=auditRender=0;
	u32 t=now-stdPhaseStart;
	if(t>=18000000u)
	{
		if(STD_ONLY>=0 || ++stdPhase>=6) { stdDone=1; return; }
		stdPhaseStart=now; t=0;
	}
	stdLastPhase=t>=2000000u ? stdPhase : -1;
	u32 active=t<2000000u ? 0 : t-2000000u;
	int step=MIN(active/250000u,64),x=step<=32 ? 32+step : 96-step;
	static const u8 editX[8]={40,47,48,63,40,47,48,63};
	int breakingX=editX[MIN(active/2000000u,7)];
	g_player.body.x=(stdPhase==0 ? 40 : stdPhase==4 ? breakingX-1 : x)*FU+FU/2;
	g_player.body.z=48*FU+FU/2; g_player.body.y=24*FU;
	g_player.body.vx=g_player.body.vy=g_player.body.vz=0;
	g_player.health=MAX_HEALTH; g_player.dead=0;
	g_player.yaw=stdPhase==0 ? ((active/2000000u)%8)*128 : stdPhase==4 ? 256 : (step<=32 ? 256 : 768);
	g_player.pitch=stdPhase==2 ? -250 : stdPhase==3 ? 250 :
	                  stdPhase==5 ? (step<32 ? -250 : 250) : stdPhase==4 ? -150 : -40;
	/* Each pair removes/restores a ground block, including X seams. Edits
	   exercise world_set + real lighting/mesh publication, not mining delays. */
	if((stdPhase==4 || stdPhase==5) && t>=2000000u && active<16000000u)
	{
		int e=MIN(active/1000000u,15);
		if((int)stdStats[stdPhase][10]<=e && !stdEditActive)
		{
			int pairStep=(e/2)*8;
			stdEditTargetX=stdPhase==4 ? editX[e/2] : pairStep<=32 ? 32+pairStep : 96-pairStep;
			stdEditTargetY=23; stdEditTargetZ=48;
			u32 *r=stdEdits[stdEditCount++];
			r[0]=stdPhase; r[1]=e; stdEditStart=std_us();
			world_set(stdEditTargetX,23,48,(e&1) ? B_GRASS : B_AIR);
			r[2]=std_us()-stdEditStart; r[5]=stdEditTargetX; r[6]=23;
			stdEditActive=1; stdEditFrames=0; stdStats[stdPhase][10]=e+1;
		}
	}
}
