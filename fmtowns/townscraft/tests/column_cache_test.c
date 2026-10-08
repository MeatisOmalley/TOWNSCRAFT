/* Cache/reference equivalence, eviction, edits, lighting and store failures. */
#define main caves_regression_main
#include "caves_test.c"
#undef main

static u8 *referenceBlocks,*referenceLight;
static void reference_world(int width,u32 seed)
{
	cacheAllocated=0; g_columnMap=NULL; g_allocChunkCount=0;
	g_W=width; g_NC=width/CS; strideZ=width*WH;
	g_blocks=calloc(width*width*WH,1); g_light=calloc(width*width*WH,1);
	g_height=calloc(width*width,1); g_zOff=calloc(width,4);
	for(int z=0; z<width; ++z) g_zOff[z]=z*strideZ;
	g_chunks=calloc(g_NC*g_NC*NCY,sizeof(Chunk));
	poolSize=mesh_pool_quads(); g_meshPool=calloc(poolSize,8);
	pq=calloc(PQ_LEN,4); rq=calloc(RQ_LEN,4); init_hides_tab();
	world_generate(seed);
	referenceBlocks=g_blocks; referenceLight=g_light;
}

static void compare_resident(int px,int pz)
{
	for(int s=0; s<g_residentColumns; ++s)
	{
		int cx=g_columnCoords[s*2],cz=g_columnCoords[s*2+1];
		if(cx<0) continue;
		for(int z=cz*CS; z<(cz+1)*CS; ++z)
		for(int x=cx*CS; x<(cx+1)*CS; ++x)
		for(int y=0; y<WH; ++y)
		{
			int ref=(z*g_W+x)*WH+y;
			check(wget(x,y,z)==referenceBlocks[ref],"resident terrain matches full reference");
			/* Outer ring may be missing light from sources beyond residence. */
			if(ABS(cx-px/CS)<=1 && ABS(cz-pz/CS)<=1)
			{
				if(world_light_at(x,y,z)!=referenceLight[ref])
				{
					printf("light mismatch %d,%d,%d got %d expected %d\n",x,y,z,world_light_at(x,y,z),referenceLight[ref]);
					check(0,"inner lighting matches full reference");
				}
			}
		}
	}
}

static u32 serialized[300000],serialPos;
static u32 pool_digest(void)
{
	u32 hash=2166136261u;
	for(int i=0;i<g_allocChunkCount;++i)
	{
		Chunk *c=&g_chunks[i];
		hash=(hash^c->count)*16777619u;
		for(int j=0;j<c->count*2;++j) hash=(hash^g_meshPool[c->off*2+j])*16777619u;
	}
	return hash;
}
static void check_pool(void)
{
	u32 quads=0,used=0;
	for(int i=0;i<g_allocChunkCount;++i)
	{
		Chunk *c=&g_chunks[i]; quads+=c->count; used+=c->cap;
		check(c->count<=c->cap && c->off+c->cap<=poolSize,"pool reservations are bounded");
		for(int j=0;j<i;++j)
		{
			Chunk *p=&g_chunks[j];
			check(!p->cap || !c->cap || p->off+p->cap<=c->off || c->off+c->cap<=p->off,
			      "pool reservations never overlap");
		}
	}
	check(quads==g_meshQuads && used==poolUsed,"pool counts match live ownership");
}
static void put_word(u32 value) { serialized[serialPos++]=value; }
static u32 get_word(void) { return serialized[serialPos++]; }
static int require_far(int *cx,int *cz)
{
	if(world_column_ready(0,0)) return 0;
	*cx=*cz=0; return 1;
}
static int pin_far(int cx,int cz) { return cx==0 && cz==0; }

#ifndef COLUMN_CACHE_FIXTURE
int main(void)
{
	for(int profile=0; profile<2; ++profile)
	{
		g_ramMB=profile ? 4 : 2;
		reference_world(profile ? 160 : 96,4242);
		world_alloc(); world_generate(4242);
		check(g_residentColumns==25 && g_allocChunkCount==78,"resident allocation is fixed");
		check(world_store_used()<columnCapacity,"baseline columns fit compressed backing");
		compare_resident(g_spawnX,g_spawnZ);
		/* A streamed seam must not turn into a frame-wide geometry burst. */
		world_stream(48,48,20,100000);
		check_pool();
		/* Compaction must not overwrite later sources when reservations grow. */
		u32 beforeCompact=pool_digest(); poolUsed=0;
		for(int i=0;i<g_allocChunkCount;++i) { g_chunks[i].cap=g_chunks[i].count; poolUsed+=g_chunks[i].cap; }
		pool_compact(); check_pool();
		check(pool_digest()==beforeCompact,"growing compacted reservations preserves every mesh");
		int deferred=0;
		for(int i=0; i<g_allocChunkCount; ++i)
			if(g_chunks[i].meshed) { dirty_layers(&g_chunks[i],0xFFFF,DIRTY_STREAM); ++deferred; }
		check(deferred>1,"budget fixture has multiple resident meshes");
		world_update_dirty_chunks(1);
		int remaining=0;
		for(int i=0; i<g_allocChunkCount; ++i) if(g_chunks[i].dirty==DIRTY_STREAM) ++remaining;
		check(remaining==deferred && meshJobLayer==4,"deferred scan advances only four layers per frame");
		for(int step=0; step<3; ++step) world_update_dirty_chunks(1);
		remaining=0;
		for(int i=0; i<g_allocChunkCount; ++i) if(g_chunks[i].dirty==DIRTY_STREAM) ++remaining;
		check(remaining==deferred-1,"four steps publish one complete deferred mesh");
		while(remaining-->0) for(int step=0; step<4; ++step) world_update_dirty_chunks(1);
		Chunk *edited=&g_chunks[chunk_index(3,2,3)];
		dirty_layers(edited,0xFFFF,DIRTY_GEOMETRY);
		world_update_dirty_chunks(0);
		check(!edited->dirty,"player geometry edits keep immediate rebuilds");
		Chunk *progressive=&g_chunks[chunk_index(3,1,3)],expected=*progressive;
		u32 *expectedQuads=calloc(expected.count,8);
		memcpy(expectedQuads,g_meshPool+expected.off*2,expected.count*8);
		dirty_layers(progressive,0xFFFF,DIRTY_STREAM);
		for(int step=0; step<3; ++step)
		{
			check(world_update_dirty_chunks(1),"partial mesh work consumes the shared frame budget");
			check(progressive->count==expected.count && !memcmp(g_meshPool+progressive->off*2,expectedQuads,expected.count*8),"old published geometry remains intact during scan");
		}
		world_update_dirty_chunks(1);
		check(progressive->count==expected.count && !memcmp(g_meshPool+progressive->off*2,expectedQuads,expected.count*8) &&
		      !memcmp(progressive->group,expected.group,sizeof(expected.group)) && !memcmp(progressive->gbox,expected.gbox,sizeof(expected.gbox)),
		      "incremental mesh exactly matches synchronous geometry and bounds");
		dirty_layers(progressive,0xFFFF,DIRTY_STREAM); world_update_dirty_chunks(1);
		check(meshJobCx>=0,"edit fixture starts with an unfinished mesh");
		world_set(49,40,49,B_GLASS);
		check(meshJobCx<0,"block edit cancels stale partial geometry");
		world_set(49,40,49,referenceBlocks[(49*g_W+49)*WH+40]);
		world_update_dirty_chunks(0);
		free(expectedQuads);
		int path[][2]={{8,8},{g_W-8,8},{g_W-8,g_W-8},{8,g_W-8},{g_W/2,g_W/2}};
		for(int repeat=0; repeat<3; ++repeat)
		for(int p=0; p<ARRAY_LEN(path); ++p)
		{
			int x=path[p][0],z=path[p][1];
			world_stream(x,z,20,100000);
			compare_resident(x,z);
			check(g_meshQuads<=poolSize,"travel stays within mesh pool");
			check_pool();
		}
		/* Motion prepares the entering strip before the actual crossing. */
		world_stream(32,48,20,100000);
		check(!world_column_ready(5,3),"prefetch fixture starts without entering strip");
		cache_stream(44,48,1);
		check(cacheLightSlot>=0 && cacheSkyCell==64 && !residentReady[cacheLightSlot],"incoming sky initialization is limited to 64 stacks");
		Chunk *waiting=&g_chunks[chunk_index(2,2,3)];
		check(waiting->meshed,"pending-light mesh fixture is present");
		dirty_layers(waiting,0xFFFF,DIRTY_LIGHT);
		u32 version=g_meshVersion;
		world_update_dirty_chunks(1);
		check(waiting->dirty==DIRTY_LIGHT && g_meshVersion==version,"meshes wait for settled streaming light");
		for(int tick=0; tick<1000 && !world_column_sim_ready(5,3); ++tick) world_stream(44,48,20,1);
		check(world_column_sim_ready(5,3),"entering column prefetched four blocks before crossing");
		for(int z=36; z<=60; ++z)
		for(int x=32; x<=56; ++x)
		for(int y=0; y<WH; ++y)
			check(world_light_at(x,y,z)==referenceLight[(z*g_W+x)*WH+y],"prefetch retains visible light envelope");
		world_stream(8,8,20,100000);
		world_set(8,40,8,B_PLANKS); world_set(9,40,8,B_TORCH);
		check(world_light_at(10,40,8)%16==13,"torch lights adjacent cell");
		world_stream(g_W-8,g_W-8,20,100000);
		check(!world_column_ready(0,0),"edited column actually evicts");
		world_stream(8,8,20,100000);
		check(wget(8,40,8)==B_PLANKS && wget(9,40,8)==B_TORCH,"edits and torch survive eviction");
		check(world_light_at(10,40,8)%16==13,"torch light survives reactivation");
		/* A block edit during an incremental sky job must preserve channel ownership. */
		cache_stream(g_W-8,g_W-8,1);
		int editing=cacheLightSlot>=0 ? cacheLightSlot : g_columnMap[0];
		world_set(g_columnCoords[editing*2]*CS+2,40,g_columnCoords[editing*2+1]*CS+2,B_GLASS);
		/* Edits queue their relight without stealing the stream job's channel.
		   Geometry still publishes immediately, before that light job settles. */
		world_update_dirty_chunks(0);
		check(!g_chunks[chunk_index(g_columnCoords[editing*2],40/CS,g_columnCoords[editing*2+1])].dirty,
		      "edit geometry publishes during pending streaming light");
		world_stream(8,8,20,100000);
		check(world_light_at(10,40,8)%16==13,"pending sky never becomes block light");
		serialPos=0; check(world_save_columns(put_word,NULL),"column stream exports");
		world_set(8,40,8,B_AIR);
		serialPos=0; world_load_position(8,8);
		check(world_load_columns(get_word,NULL),"column stream imports");
		world_rebuild_after_load();
		check(wget(8,40,8)==B_PLANKS && world_light_at(10,40,8)%16==13,"column save restores terrain and light");
		/* Corrupt incoming backing must retain the slot's previous owner. */
		int incoming=(g_NC-1)*g_NC+g_NC-1,slot=g_columnMap[0];
		ColumnRecord saved=columnRecords[incoming];
		columnRecords[incoming].sum^=1;
		check(!cache_load(g_NC-1,g_NC-1,slot) && g_columnMap[0]==slot && wget(8,40,8)==B_PLANKS,"corruption cannot evict valid terrain");
		columnRecords[incoming]=saved;
		/* Full backing cannot discard an outgoing edit. */
		world_set(8,40,8,B_STONE);
		column_compact(-1);
		u32 capacity=columnCapacity; columnCapacity=columnTop;
		for(int i=0; i<COLUMN_CELLS; ++i) g_blocks[slot*COLUMN_CELLS+i]=(i&1) ? B_STONE : B_PLANKS;
		residentDirty[slot]=1;
		check(!cache_load(g_NC-1,g_NC-1,slot) && g_columnMap[0]==slot && residentDirty[slot],"full store retains dirty column");
		columnCapacity=capacity;
		world_stream(48,48,20,100000);
		check(!world_column_ready(0,0),"far fuse fixture starts without its terrain");
		world_set_column_pin(pin_far); world_set_column_needed(require_far);
		for(int tick=0; tick<1000 && !world_column_sim_ready(0,0); ++tick) world_stream(48,48,20,1);
		check(world_column_sim_ready(0,0) && g_residentColumns==25,"far required terrain loads without expanding cache");
		world_set_column_pin(NULL); world_set_column_needed(NULL);
		printf("PASS %u MB: bounded seam rebuilds, reference terrain/light, repeated travel, edits/torch, column serialization and failed eviction; backing %u bytes\n",g_ramMB,world_store_used());
	}
	/* Sources on both sides of column seams and shaded cave entrances must
	   produce the same light after incremental preparation as a full world. */
	g_ramMB=2; reference_world(96,123456789);
	int torches[][3]={{31,40,48},{48,40,47},{50,40,64}};
	for(int t=0; t<ARRAY_LEN(torches); ++t) world_set(torches[t][0],torches[t][1],torches[t][2],B_TORCH);
	world_alloc(); world_generate(123456789);
	for(int t=0; t<ARRAY_LEN(torches); ++t)
	{
		world_stream(torches[t][0],torches[t][2],20,100000);
		world_set(torches[t][0],torches[t][1],torches[t][2],B_TORCH);
	}
	int seams[][2]={{8,8},{31,48},{48,47},{50,64},{88,88},{48,48}};
	for(int p=0; p<ARRAY_LEN(seams); ++p)
	{
		int x=seams[p][0],z=seams[p][1],ready=0;
		/* This fixture requests the stationary window; directional prefetch
		   intentionally shifts its outer ring and is checked separately. */
		cacheLastX=cacheLastZ=-1; cacheDirectionX=cacheDirectionZ=0;
		for(int tick=0; tick<1000 && !ready; ++tick)
		{
			world_update_dirty_chunks(1); world_stream(x,z,20,1);
			ready=cacheLightSlot<0;
			for(int cz=MAX(0,z/CS-2); cz<=MIN(g_NC-1,z/CS+2); ++cz)
			for(int cx=MAX(0,x/CS-2); cx<=MIN(g_NC-1,x/CS+2); ++cx)
				if(!world_column_sim_ready(cx,cz)) ready=0;
		}
		check(ready,"incremental seam fixture settles");
		compare_resident(x,z);
	}
	printf("PASS: incremental skylight and torch seams match full-world reference\n");
	return 0;
}
#endif
