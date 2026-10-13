/* 1024-block HDD world: immutable base, two edit slots, compact save index.
 * Included after column_cache.inc by the opt-in DOS large-world overlay. */
static u32 large_column_offset(int id,int slot)
{
	return (33u+(u32)id*TERRAIN_SECTORS*3u+(u32)slot*TERRAIN_SECTORS)*512u;
}

static void world_load_prebaked(void)
{
	if(!terrainLarge || !terrainActive) fatal("1024-world HDD is unavailable");
	terrain_reset();
	genSeed=terrainSeed; genVersion=2;
	columnTop=0; preparedColumn=-1;
	for(int id=0; id<TERRAIN_COLUMNS; ++id)
	{
		ColumnRecord *r=&columnRecords[id];
		memset(r,0,sizeof(*r));
		r->off=large_column_offset(id,0);
		r->len=COLUMN_CELLS; r->sum=terrainBaseSum[id];
		r->raw=1; r->disk=TERRAIN_DISK;
	}
	g_spawnX=terrainSpawnX; g_spawnZ=terrainSpawnZ;
	terrain_cache_allocate();
	cache_reset();
	cacheBoot=1;
	cache_stream(g_spawnX,g_spawnZ,100000);
	cacheBoot=0;
	if(streamError || !world_column_ready(g_spawnX/CS,g_spawnZ/CS))
		fatal("Cannot read 1024-world spawn terrain");
	g_spawnY=world_surface_y(g_spawnX,g_spawnZ);
#ifdef DOS_STREAM_LIGHT_CACHE
    if(!streamLightBaked) light_init();
#else
	light_init();
#endif
	trackLightDirty=1;
	terrainBlocking=0;
}

int world_save_large_columns(void (*put)(u32))
{
	if(!terrainLarge || !world_store_flush()) return 0;
	put(terrainSeed);
	put(column_checksum((const u8 *)terrainBaseSum,sizeof(terrainBaseSum)));
	put(TERRAIN_COLUMNS);
	for(int id=0; id<TERRAIN_COLUMNS; ++id)
	{
		const ColumnRecord *r=&columnRecords[id];
		u32 first=large_column_offset(id,0),slot;
		if(r->disk!=TERRAIN_DISK || r->off<first ||
		   r->off>large_column_offset(id,2) ||
		   (r->off-first)%COLUMN_CELLS || !r->len ||
		   r->len>COLUMN_CELLS || r->raw>1) return 0;
		slot=(r->off-first)/COLUMN_CELLS;
		put(slot); put(r->len); put(r->sum); put(r->raw);
	}
	return 1;
}

int world_load_large_columns(u32 (*get)(void))
{
	if(!terrainLarge || !terrainActive) return 0;
	u32 seed=get(),indexSum=get(),count=get();
	if(seed!=terrainSeed || count!=TERRAIN_COLUMNS ||
	   indexSum!=column_checksum((const u8 *)terrainBaseSum,sizeof(terrainBaseSum)))
		return 0;
	terrain_reset();
	columnTop=0; preparedColumn=-1;
	cache_reset();
	for(int id=0; id<TERRAIN_COLUMNS; ++id)
	{
		u32 slot=get(),len=get(),sum=get(),raw=get();
		ColumnRecord *r=&columnRecords[id];
		if(slot>2 || !len || len>COLUMN_CELLS || raw>1 ||
		   (raw && len!=COLUMN_CELLS) ||
		   (slot==0 && (len!=COLUMN_CELLS || !raw || sum!=terrainBaseSum[id])))
			return 0;
		memset(r,0,sizeof(*r));
		r->off=large_column_offset(id,slot);
		r->len=len; r->sum=sum; r->raw=raw; r->disk=TERRAIN_DISK;
		terrainPinSlot[id]=slot;
#ifdef DOS_STREAM_LIGHT_CACHE
        if(slot) stream_light_invalidate((id%g_NC)*CS,(id/g_NC)*CS);
#endif
	}
	terrain_cache_allocate();
	cacheBoot=1;
	cache_stream(loadX,loadZ,100000);
	cacheBoot=0;
	loadPosition=0;
	terrainBlocking=0;
	return !streamError && world_column_ready(loadX/CS,loadZ/CS);
}

void world_commit_large_columns(void)
{
	if(!terrainLarge) return;
	for(int id=0; id<TERRAIN_COLUMNS; ++id)
		terrainPinSlot[id]=(columnRecords[id].off-large_column_offset(id,0))/COLUMN_CELLS;
}

/* Smoke-only caller: force cold corner loads and a return to the edited
 * spawn column. Ordinary gameplay never invokes this blocking traversal. */
int world_verify_large_travel(int x,int z)
{
	int near,far,back;
	if(!terrainLarge) return 0;
	terrainBlocking=1;
	cacheBoot=1;
	cache_stream(0,0,100000);
	near=world_column_ready(0,0);
	cache_stream(g_W-1,g_W-1,100000);
	far=world_column_ready(g_NC-1,g_NC-1);
	cache_stream(x,z,100000);
	back=world_column_ready(x/CS,z/CS);
	cacheBoot=0;
	terrainBlocking=0;
	return near && far && back && !streamError;
}
