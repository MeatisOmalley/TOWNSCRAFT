/* Test-only instrumentation, injected into copies of historical game.c. */
u32 auditRows[768][10],auditCount,auditDone,auditInfo[8];
u32 auditUpdate,auditRender;
static u32 auditStart,auditLast;
static void audit_tick(void)
{
	u32 now;
	if(GS_TITLE==state)
	{
		new_game(4242); state=GS_PLAY;
		g_renderScale=2; g_viewDist=8; g_flatDist=0;
		AUDIT_INTERLACE_OFF;
		mobs_clear(); g_time=1000;
		auditInfo[0]=g_W; auditInfo[1]=g_spawnX;
		auditInfo[2]=g_spawnY; auditInfo[3]=g_spawnZ;
		auditInfo[4]=g_renderScale; auditInfo[5]=g_viewDist;
		/* Capture the authoritative terrain around spawn before movement. */
		u32 hash=2166136261u;
		for(int z=MAX(0,g_spawnZ-8);z<=MIN(g_W-1,g_spawnZ+8);++z)
		for(int x=MAX(0,g_spawnX-8);x<=MIN(g_W-1,g_spawnX+8);++x)
		for(int y=0;y<WH;++y) hash=(hash^wget(x,y,z))*16777619u;
		auditInfo[6]=hash;
		extern int g_profEnable; extern u32 g_profCount;
		g_profEnable=0; g_profCount=0;
		auditStart=g_ticks; auditLast=0;
	}
	if(GS_PLAY!=state || auditDone) return;
	now=g_ticks;
	if(auditLast && auditCount<768)
	{
		u32 *r=auditRows[auditCount++];
		r[0]=auditLast-auditStart; r[1]=now-auditLast;
		r[2]=auditUpdate; r[3]=auditRender;
		r[4]=g_statFaces; r[5]=g_statItems;
		r[6]=g_meshQuads; r[7]=g_meshVersion;
		int dirty=0,missing=0;
		for(int cz=MAX(0,(g_player.body.z/FU-8)/CS);cz<=MIN(g_NC-1,(g_player.body.z/FU+8)/CS);++cz)
		for(int cx=MAX(0,(g_player.body.x/FU-8)/CS);cx<=MIN(g_NC-1,(g_player.body.x/FU+8)/CS);++cx)
		for(int cy=0;cy<NCY;++cy)
		{
			Chunk *c=&g_chunks[chunk_index(cx,cy,cz)];
			dirty+=c->dirty!=0; missing+=AUDIT_UNMESHED;
		}
		r[8]=dirty; r[9]=missing;
	}
	auditLast=now; auditUpdate=auditRender=0;
	u32 t=now-auditStart;
	if(t>=3200 || auditCount>=768) { auditDone=1; return; }
	if(t<1200)
	{
		g_player.body.x=g_spawnX*FU+FU/2;
		g_player.body.z=g_spawnZ*FU+FU/2;
		g_player.yaw=t<200 ? 0 : ((t-200)/125%8)*128;
		g_player.pitch=-40;
	}
	else
	{
		int step=CLAMP(((int)t-1200)/25,0,80);
		int x=step<=40 ? 32+step : 112-step;
		g_player.body.x=x*FU+FU/2;
		g_player.body.z=48*FU+FU/2;
		g_player.yaw=step<=40 ? 256 : 768;
		g_player.pitch=-40;
	}
	g_player.body.y=world_surface_y(g_player.body.x/FU,g_player.body.z/FU)*FU;
	g_player.body.vx=g_player.body.vy=g_player.body.vz=0;
	g_player.health=MAX_HEALTH; g_player.dead=0;
}
