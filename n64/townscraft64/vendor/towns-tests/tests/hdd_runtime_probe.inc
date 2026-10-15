/* Test-only real-emulator edit/evict/revisit fixture. Not production code. */
u32 hddProbeDone,hddProbePhase,hddProbeInfo[8];
u32 auditUpdate,auditRender;
static u32 hddProbeStart,hddProbeWrites;
static int hdd_probe_ready(int x,int z)
{
	for(int cz=MAX(0,z/CS-1); cz<=MIN(g_NC-1,z/CS+1); ++cz)
	for(int cx=MAX(0,x/CS-1); cx<=MIN(g_NC-1,x/CS+1); ++cx)
		if(!world_column_sim_ready(cx,cz)) return 0;
	return 1;
}
static void audit_tick(void)
{
	if(GS_TITLE==state)
	{
		new_game(4242); mobs_clear(); state=GS_PLAY;
		g_renderScale=2; g_viewDist=8; g_flatDist=0;
		g_time=6000; g_skyDarken=0;
		if(!world_hdd_active() || g_W!=256) fatal("HDD probe needs 8MB and scratch disk");
		hddProbeStart=g_ticks; hddProbeWrites=g_terrainDiskStats[1];
	}
	if(hddProbeDone || GS_PLAY!=state) return;
	if(g_terrainDiskStats[3]) fatal("HDD probe storage error");
	if(g_ticks-hddProbeStart>30000) fatal("HDD probe travel timed out");
	int far=hddProbePhase==1;
	int x=far ? 248 : 8,z=far ? 248 : 8;
	g_player.body.x=x*FU+FU/2; g_player.body.z=z*FU+FU/2;
	g_player.body.y=40*FU; g_player.body.vx=g_player.body.vy=g_player.body.vz=0;
	g_player.yaw=far ? 768 : 256; g_player.pitch=-40;
	g_player.health=MAX_HEALTH; g_player.dead=0;
	if(!hdd_probe_ready(x,z)) return;
	if(hddProbePhase==0)
	{
		world_set(8,40,8,B_GLASS); world_set(9,40,8,B_TORCH);
		hddProbePhase=1;
	}
	else if(hddProbePhase==1)
	{
		if(world_column_ready(0,0)) return;
		if(g_terrainDiskStats[1]<=hddProbeWrites) fatal("HDD probe edit never reached disk");
		hddProbePhase=2;
	}
	else
	{
		if(wget(8,40,8)!=B_GLASS || wget(9,40,8)!=B_TORCH || (world_light_at(10,40,8)&15)!=13)
			fatal("HDD probe revisit lost edit or light");
		hddProbeInfo[0]=g_W; hddProbeInfo[1]=g_ticks-hddProbeStart;
		for(int i=0; i<4; ++i) hddProbeInfo[2+i]=g_terrainDiskStats[i];
		hddProbeInfo[6]=wget(8,40,8); hddProbeInfo[7]=world_light_at(10,40,8)&15;
		hddProbeDone=1;
	}
}
