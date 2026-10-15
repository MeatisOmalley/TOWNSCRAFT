/* Test-only world.c instrumentation. Never linked into production builds.
   Identical flat terrain, edit positions, lighting and 27 published chunks
   isolate edit latency/work from generator, renderer and mob differences. */
u32 auditMesh[64][10],auditMeshCount,auditMeshDone,auditMemory[12];
u32 auditMeshHashes[64];
static u32 audit_us(void)
{
	static u32 prevTick,prevLow,total;
	u32 tick=g_ticks;
	u16 low=inw(0x26);
	u32 delta=(u16)(low-(u16)prevLow);
	int estimate=(int)(tick-prevTick)*10000-(int)delta;
	total+=delta+(estimate>=0 ? (u32)((estimate+32768)>>16)<<16 : 0);
	prevTick=tick; prevLow=low;
	return total;
}
static int audit_dirty(void)
{
	int n=0;
	for(int cz=1;cz<=3;++cz) for(int cx=1;cx<=3;++cx)
	for(int cy=0;cy<NCY;++cy)
		n+=g_chunks[chunk_index(cx,cy,cz)].dirty!=0;
	return n;
}
static u32 audit_mesh_hash(void)
{
	u32 hash=2166136261u;
	for(int cz=1;cz<=3;++cz) for(int cx=1;cx<=3;++cx)
	for(int cy=0;cy<NCY;++cy)
	{
		Chunk *c=&g_chunks[chunk_index(cx,cy,cz)];
		hash=(hash^c->count)*16777619u;
		for(int i=0;i<c->count*2;++i)
			hash=(hash^g_meshPool[c->off*2+i])*16777619u;
	}
	return hash;
}
void audit_world_probe(void)
{
	auditMemory[0]=g_W;
	auditMemory[1]=heap_high_free(); auditMemory[2]=heap_low_free();
	auditMemory[3]=sizeof(Chunk); auditMemory[4]=poolSize*8;
	AUDIT_MEMORY;
	AUDIT_SETTLE;
	AUDIT_LINEAR;
	auditMemory[11]=g_W;
	/* Only the common central region participates. Cached columns outside
	   it and nonresident boundaries cannot affect its face/light results. */
	for(int z=0;z<g_W;++z) for(int x=0;x<g_W;++x)
	{
		if(!AUDIT_READY) continue;
		g_height[AUDIT_HINDEX]=24;
		for(int y=0;y<WH;++y)
		{
			int i=widx(x,y,z);
			g_blocks[i]=y<24 ? B_STONE : B_AIR;
			g_light[i]=y<24 ? 0 : 0xf0;
		}
	}
	mesh_reset();
	for(int cz=1;cz<=3;++cz) for(int cx=1;cx<=3;++cx)
	for(int cy=0;cy<NCY;++cy) { AUDIT_FULL; }
	static const int edits[10][4]={
		{40,25,40,B_STONE},{40,25,40,B_AIR},
		{47,25,40,B_STONE},{47,25,40,B_AIR},
		{47,25,47,B_STONE},{47,25,47,B_AIR},
		{40,23,40,B_AIR},{40,23,40,B_STONE},
		{40,25,40,B_TORCH},{40,25,40,B_AIR}
	};
	audit_us();
	for(int repeat=0;repeat<3;++repeat) for(int e=0;e<10;++e)
	{
		const int *p=edits[e];
		u32 *r=auditMesh[auditMeshCount++],start=audit_us();
		r[0]=e;
		world_set(p[0],p[1],p[2],p[3]);
		r[1]=audit_us()-start;
		r[7]=audit_dirty();
		for(int cz=1;cz<=3;++cz) for(int cx=1;cx<=3;++cx)
		for(int cy=0;cy<NCY;++cy)
		{
			Chunk *c=&g_chunks[chunk_index(cx,cy,cz)];
			if(c->dirty) r[6]+=AUDIT_LAYERS;
		}
		Chunk *target=&g_chunks[chunk_index(p[0]/CS,p[1]/CS,p[2]/CS)];
		while(audit_dirty() && r[5]<256)
		{
			start=audit_us();
			world_update_dirty_chunks(AUDIT_BUDGET);
			u32 elapsed=audit_us()-start;
			r[2]+=elapsed; r[3]=MAX(r[3],elapsed); ++r[5];
			if(!r[4] && !target->dirty) r[4]=r[5];
		}
		r[8]=g_meshQuads; r[9]=audit_dirty();
		u32 hash=audit_mesh_hash();
		auditMeshHashes[auditMeshCount-1]=hash;
		for(int cz=1;cz<=3;++cz) for(int cx=1;cx<=3;++cx)
		for(int cy=0;cy<NCY;++cy) { AUDIT_FULL; }
		if(hash!=audit_mesh_hash()) r[9]|=2;
	}
	auditMeshDone=1;
}
