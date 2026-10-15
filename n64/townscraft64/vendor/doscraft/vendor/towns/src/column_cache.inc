/* Resident columns preserve global coordinates while reusing physical slots. */
static int (*columnPin)(int,int);
static int (*columnNeeded)(int *,int *);
static int cacheBoot;
static int cacheLightSlot=-1,cacheLightChannel;
static int cacheLightSeed,cacheSkyCell;
static int loadX,loadZ,loadPosition;
static int cacheLastX=-1,cacheLastZ=-1,cacheDirectionX,cacheDirectionZ;

static void cache_dirty_seam(int cx,int cz)
{
	if(cx<0 || cz<0 || cx>=g_NC || cz>=g_NC || !world_column_ready(cx,cz)) return;
	for(int cy=0; cy<NCY; ++cy)
	{
		Chunk *c=&g_chunks[chunk_index(cx,cy,cz)];
		/* Arriving terrain changes border visibility. Rebuild it within the
		   travel budget; player edits retain their immediate priority. */
		if(c->meshed) dirty_layers(c,0xFFFF,DIRTY_STREAM);
	}
	if(meshJobCx==cx && meshJobCz==cz) mesh_cancel();
}

static int cache_light_pending(void) { return cacheLightSlot>=0; }

/* Open sky above every neighboring obstruction is already level 15.
   Only cells below that height can seed a useful horizontal flood. */
static void cache_seed_light(int cx,int cz,int ch,int stacks)
{
	int end=MIN(cacheLightSeed+stacks,(CS+2)*(CS+2));
	for(; cacheLightSeed<end; ++cacheLightSeed)
	{
		int x=cx*CS-1+cacheLightSeed%(CS+2),z=cz*CS-1+cacheLightSeed/(CS+2);
		if(!in_world(x,0,z) || !world_column_ready(x/CS,z/CS)) continue;
		int base=widx(x,0,z);
		if(ch)
		{
			int incoming=x>=cx*CS && x<(cx+1)*CS && z>=cz*CS && z<(cz+1)*CS;
			for(int y=0; y<WH; ++y)
			{
				int i=base+y;
				if(incoming)
				{
					int e=g_blockDef[BLK_ID(g_blocks[i])].lightEmit;
					if(e) { lset(i,1,e); pq_push(i); }
				}
				else if(lget(i,1)>1) pq_push(i);
			}
			continue;
		}
		/* Resolve horizontal neighbors once per stack, not six times per
		   lit voxel. Missing columns and world edges cannot accept light. */
		int nb[4]={-1,-1,-1,-1},mh=world_height(x,z);
		for(int d=0; d<4; ++d)
		{
			int nx=x+(d==1)-(d==0),nz=z+(d==3)-(d==2);
			if(in_world(nx,0,nz) && world_column_ready(nx/CS,nz/CS))
			{
				nb[d]=widx(nx,0,nz);
				mh=MAX(mh,world_height(nx,nz));
			}
		}
		for(int y=0; y<mh && y<WH; ++y)
		{
			int i=base+y,L=lget(i,0);
			if(!L) continue;
			/* Enqueue only cells that can change a neighbor. Flooding the
			   entire open sky would nearly fill PQ before useful work starts. */
			for(int d=0; d<6; ++d)
			{
				int n;
				if(d==2) { if(!y) continue; n=i-1; }
				else if(d==3) { if(y+1>=WH) continue; n=i+1; }
				else { int b=nb[d<2 ? d : d-2]; if(b<0) continue; n=b+y; }
				int op=g_lightOpacity[BLK_ID(g_blocks[n])],nl=L-1-op;
				if(d==2 && L==15 && !op) nl=15;
				if(nl>lget(n,0)) { pq_push(i); break; }
			}
		}
	}
}

static void cache_init_sky(int slot,int stacks)
{
	int cx=g_columnCoords[slot*2],cz=g_columnCoords[slot*2+1];
	int end=MIN(cacheSkyCell+stacks,CS*CS);
	for(; cacheSkyCell<end; ++cacheSkyCell)
	{
		int x=cx*CS+(cacheSkyCell&15),z=cz*CS+(cacheSkyCell>>4),L=15,h=0;
		u32 base=slot*COLUMN_CELLS+cacheSkyCell*WH;
		for(int y=WH-1; y>=0; --y)
		{
			int op=g_lightOpacity[BLK_ID(g_blocks[base+y])];
			if(op)
			{
				if(!h) h=y+1;
				L=op>=15 ? 0 : MAX(0,L-1-op);
			}
			g_light[base+y]=L<<4;
		}
		g_height[height_index(x,z)]=h;
	}
}

/* PQ belongs to one channel at a time, including between frames. */
static void cache_light_pump(int budget)
{
	while(cacheLightSlot>=0)
	{
		int slot=cacheLightSlot,cx=g_columnCoords[slot*2],cz=g_columnCoords[slot*2+1];
		if(cacheSkyCell<CS*CS)
		{
			cache_init_sky(slot,budget ? 64 : CS*CS);
			if(budget) return;
		}
		if(cacheLightSeed<(CS+2)*(CS+2))
		{
			cache_seed_light(cx,cz,cacheLightChannel,budget && !cacheLightChannel ? 64 : (CS+2)*(CS+2));
			if(budget) return;
		}
		lightBudget=budget; propagate(cacheLightChannel); lightBudget=0;
		if(pqHead!=pqTail) return;
		if(!cacheLightChannel)
		{
			cacheLightChannel=1;
			cacheLightSeed=0;
			if(budget) return;
			continue;
		}
		cacheLightSlot=-1;
		residentReady[slot]=1;
		/* The new column has no mesh yet; world_stream builds it normally.
		   Only existing neighbors need their shared faces refreshed. */
		cache_dirty_seam(cx-1,cz); cache_dirty_seam(cx+1,cz);
		cache_dirty_seam(cx,cz-1); cache_dirty_seam(cx,cz+1);
		/* Block edits defer their flood fill while this shared queue is busy. */
		apply_pending_light_edits();
	}
}

void world_set_column_pin(int (*pin)(int,int)) { columnPin=pin; }
void world_set_column_needed(int (*needed)(int *,int *)) { columnNeeded=needed; }
int world_cache_active(void) { return cacheAllocated; }
int world_stream_error(void) { return streamError; }
u32 world_store_used(void)
{
	u32 n=0; int i;
	for(i=0; i<g_NC*g_NC; ++i) n+=columnRecords[i].len;
	return n;
}

static void generation_begin(void)
{
	(void)terrain_quiesce(); /* Release the old world's DMA before new writes. */
	if(terrainActive) terrain_reset();
	u32 cells=g_W*g_W*WH,bytes=cells+g_W*g_W;
	generationTemporary=bytes>COLUMN_CELLS*g_residentColumns*2;
	generationMark=heap_high_mark();
	g_blocks=generationTemporary ? heap_alloc_high(bytes) : residentBlocks;
	g_height=g_blocks+cells;
	g_columnMap=NULL;
	memset(columnRecords,0,sizeof(columnRecords)); columnTop=0;
	preparedColumn=-1;
	memset(residentDirty,0,sizeof(residentDirty));
	streamError=0;
}

static void cache_reset(void)
{
	g_blocks=residentBlocks; g_light=residentLights; g_height=residentHeights;
	g_columnMap=columnMapStorage; g_columnCoords=columnCoordsStorage;
	memset(columnMapStorage,0xFF,sizeof(columnMapStorage));
	memset(columnCoordsStorage,0xFF,sizeof(columnCoordsStorage));
	memset(residentHeights,WH,(g_residentColumns+1)*CS*CS);
	memset(residentDirty,0,sizeof(residentDirty));
	memset(residentReady,0,sizeof(residentReady));
	pqHead=pqTail=rqHead=rqTail=0;
	cacheLightSlot=-1;
	cacheLastX=cacheLastZ=-1; cacheDirectionX=cacheDirectionZ=0;
	mesh_reset();
}

static int generation_finish(void)
{
	int cx,cz;
	terrainBlocking=1;
	for(cz=0; cz<g_NC; ++cz)
	for(cx=0; cx<g_NC; ++cx)
		if(!column_capture(cx,cz))
		{
			if(generationTemporary) heap_high_rewind(generationMark);
			cache_reset(); loadPosition=0; terrainBlocking=0; return 0;
		}
	if(generationTemporary) heap_high_rewind(generationMark);
	terrain_cache_allocate();
	cache_reset();
	cacheBoot=1; cache_stream(loadPosition ? loadX : g_spawnX,loadPosition ? loadZ : g_spawnZ,100000); cacheBoot=0;
	loadPosition=0;
	light_init();
	/* Restore all torch sources too (imported worlds may have them). */
	for(int s=0; s<g_residentColumns; ++s)
	{
		if(g_columnCoords[s*2]<0) continue;
		for(int i=s*COLUMN_CELLS; i<(s+1)*COLUMN_CELLS; ++i)
		{
			int e=g_blockDef[BLK_ID(g_blocks[i])].lightEmit;
			if(e) { lset(i,1,e); pq_push(i); propagate(1); }
		}
	}
	terrainBlocking=0;
	return 1;
}

static int cache_load(int cx,int cz,int slot)
{
	int ox=g_columnCoords[slot*2],oz=g_columnCoords[slot*2+1];
	/* Validate before releasing the old owner; a bad record retains it. */
	if(!column_valid(cx,cz)) { streamError=1; return 0; }
	if(ox>=0)
	{
		if(residentDirty[slot] && !column_capture(ox,oz))
		{
			if(!terrainActive) streamError=2;
			return 0;
		}
		unmesh_column(ox,oz);
		g_columnMap[oz*g_NC+ox]=-1;
		/* The outgoing terrain becomes solid/missing to its retained
		   neighbors. Invalidate those faces as well as incoming seams. */
		cache_dirty_seam(ox-1,oz); cache_dirty_seam(ox+1,oz);
		cache_dirty_seam(ox,oz-1); cache_dirty_seam(ox,oz+1);
	}
	column_restore_valid(cx,cz,g_blocks+slot*COLUMN_CELLS);
	g_columnCoords[slot*2]=cx; g_columnCoords[slot*2+1]=cz;
	g_columnMap[cz*g_NC+cx]=slot; residentDirty[slot]=0;
	residentReady[slot]=cacheBoot;
	memset(g_light+slot*COLUMN_CELLS,0,COLUMN_CELLS);
	cacheSkyCell=0; cacheLightSeed=0;
	if(cacheBoot) { cache_init_sky(slot,CS*CS); return 1; }
	/* Seed the new column and both sides of each seam. Queue pumping is
	   bounded during travel; block edits still use the exact full flood. */
	cacheLightSlot=slot; cacheLightChannel=0;
	cache_light_pump(2048);
	return 1;
}

static void cache_stream(int x,int z,int budget)
{
	terrainBlocking=budget>100;
	if(terrainActive && terrainWriteID>=0 && terrain_finish_write()<=0) return;
	int pcx=CLAMP(x/CS,0,g_NC-1),pcz=CLAMP(z/CS,0,g_NC-1);
	if(cacheLastX>=0 && x!=cacheLastX) cacheDirectionX=x>cacheLastX ? 1 : -1;
	if(cacheLastZ>=0 && z!=cacheLastZ) cacheDirectionZ=z>cacheLastZ ? 1 : -1;
	cacheLastX=x; cacheLastZ=z;
	/* Start preparing the entering strip four blocks before crossing.
	   The retained side remains beyond the 12-block view plus light reach. */
	if(!cacheBoot && budget<=100)
	{
		if(cacheDirectionX>0 && (x&15)>=12) pcx=MIN(pcx+1,g_NC-1);
		if(cacheDirectionX<0 && (x&15)<=3) pcx=MAX(pcx-1,0);
		if(cacheDirectionZ>0 && (z&15)>=12) pcz=MIN(pcz+1,g_NC-1);
		if(cacheDirectionZ<0 && (z&15)<=3) pcz=MAX(pcz-1,0);
	}
	/* Finish a stable mesh before starting another lighting job. */
	if(!cacheBoot && budget<=100 && meshJobCx>=0 && cacheLightSlot<0) return;
	if(!cacheBoot && cacheLightSlot>=0)
	{
		cache_light_pump(budget>100 ? 0 : 2048);
		if(cacheLightSlot>=0) return;
		/* Let deferred meshes see one settled-light frame between jobs. */
		if(budget<=100) return;
	}
	while(budget-->0)
	{
		int cx,cz,bx=-1,bz=0,bd=0x7FFFFFFF,slot=-1,fd=-1;
		int critical=!cacheBoot && columnNeeded && columnNeeded(&bx,&bz);
		if(!critical)
		{
			for(cz=MAX(0,pcz-2); cz<=MIN(g_NC-1,pcz+2); ++cz)
			for(cx=MAX(0,pcx-2); cx<=MIN(g_NC-1,pcx+2); ++cx)
			{
				int d=column_dist2(cx,cz,x,z);
				if(!world_column_ready(cx,cz) && d<bd) { bx=cx; bz=cz; bd=d; }
			}
		}
		if(bx<0) return;
		for(int s=0; s<g_residentColumns; ++s)
		{
			cx=g_columnCoords[s*2]; cz=g_columnCoords[s*2+1];
			if(cx<0) { slot=s; break; }
			int d=column_dist2(cx,cz,x,z);
			if(ABS(cx-pcx)<=2 && ABS(cz-pcz)<=2 && (!critical || d<=26*26)) continue;
			if(!cacheBoot && columnPin && columnPin(cx,cz)) continue;
			if(!cacheBoot && meshJobCx==cx && meshJobCz==cz) continue;
			if(d>fd) { slot=s; fd=d; }
		}
		if(slot<0 || !column_prepare(bx,bz) || !cache_load(bx,bz,slot)) return;
		streamReady=0;
		if(!cacheBoot && cacheLightSlot>=0)
		{
			if(budget>100) cache_light_pump(0);
			else return;
		}
	}
}

int world_store_flush(void)
{
	if(!cacheAllocated) return 1;
	if(!terrain_quiesce()) return 0;
	terrainBlocking=1;
	for(int s=0; s<g_residentColumns; ++s)
	{
		if(residentDirty[s])
		{
			if(!column_capture(g_columnCoords[s*2],g_columnCoords[s*2+1])) { terrainBlocking=0; return 0; }
			residentDirty[s]=0;
		}
	}
	terrainBlocking=0;
	return 1;
}

void world_load_position(int x,int z)
{
	loadX=x; loadZ=z; loadPosition=1;
}

void world_import_begin(void)
{
	genSeed=0; genVersion=0; /* Imported exact baseline; original seed unknown. */
	if(cacheAllocated) generation_begin();
}

int world_import_end(int ok,int x,int z)
{
	if(!cacheAllocated) return ok;
	if(ok) { world_load_position(x,z); return generation_finish(); }
	else
	{
		if(generationTemporary) heap_high_rewind(generationMark);
		cache_reset();
	}
	return 0;
}

void world_set_backing_reader(int (*read)(u32,u32,u8 *)) { columnRead=read; }

int world_backing_bank(u32 bankBytes)
{
	for(int id=0; id<g_NC*g_NC; ++id)
		if(columnRecords[id].disk==1) return columnRecords[id].off/bankBytes;
	return -1;
}

/* Large legacy maps can exceed one transactional floppy bank. Preserve
   their existing full-disk save format, with a bounded compressed source. */
int world_materialize_columns(void)
{
	if(!cacheAllocated) return 1;
	cache_light_pump(0);
	if(!world_store_flush() || world_store_used()>columnCapacity) return 0;
	column_compact(-1);
	for(int id=0; id<g_NC*g_NC; ++id)
	{
		ColumnRecord *r=&columnRecords[id];
		if(!r->disk) continue;
		int status;
		do { status=column_read_record(r); } while(status==0);
		if(status!=1 || !column_valid_bytes(r,columnScratch)) return 0;
		memcpy(columnArena+columnTop,columnScratch,r->len);
		r->off=columnTop; columnTop+=r->len; r->disk=0;
	}
	preparedColumn=-1;
	return 1;
}

int world_save_legacy_columns(void (*put)(int))
{
	int last=-1,count=0;
	for(int cz=0; cz<g_NC; ++cz)
	for(int lz=0; lz<CS; ++lz)
	for(int cx=0; cx<g_NC; ++cx)
	{
		int id=cz*g_NC+cx,begin=lz*CS*WH,end=begin+CS*WH,index=0;
		ColumnRecord *r=&columnRecords[id];
		const u8 *p=column_bytes(id);
		if(!p || !column_valid_bytes(r,p)) return 0;
		for(u32 k=0; k<r->len && index<end; )
		{
			int run=r->raw ? 1 : p[k++],b=p[k++];
			int from=MAX(index,begin),to=MIN(index+run,end);
			for(int i=from; i<to; ++i)
			{
				if(b!=last || count==255)
				{
					if(count) { put(count); put(last); }
					last=b; count=0;
				}
				++count;
			}
			index+=run;
		}
	}
	if(count) { put(count); put(last); }
	return 1;
}

void world_commit_columns(void)
{
	for(int id=0; id<g_NC*g_NC; ++id)
	{
		ColumnRecord *r=&columnRecords[id];
		r->off=r->pendingOff; r->len=r->pendingLen; r->sum=r->pendingSum; r->raw=r->pendingRaw; r->disk=1;
	}
	columnTop=0; preparedColumn=-1;
	memset(residentDirty,0,sizeof(residentDirty));
	streamError=0;
}

int world_save_columns(void (*put)(u32),u32 (*position)(void))
{
	if(!terrain_quiesce()) return 0;
	cache_light_pump(0);
	preparedColumn=-1;
	put(genSeed); put(genVersion); put(g_NC*g_NC);
	for(int id=0; id<g_NC*g_NC; ++id)
	{
		ColumnRecord *r=&columnRecords[id];
		ColumnRecord encoded=*r;
		int slot=g_columnMap[id];
		const u8 *p;
		if(slot>=0 && residentDirty[slot])
		{
			column_encode(id%g_NC,id/g_NC,columnEncodeScratch,&encoded);
			p=columnEncodeScratch;
		}
		else if(r->disk)
		{
			int status;
			do { status=column_read_record(r); } while(status==0);
			if(status!=1) return 0;
			p=columnScratch; preparedColumn=-1;
		}
		else p=column_bytes(id);
		if(!p || !column_valid_bytes(&encoded,p)) return 0;
		put(encoded.len); put(encoded.sum); put(encoded.raw);
		if(position)
		{
			r->pendingOff=position(); r->pendingLen=encoded.len;
			r->pendingSum=encoded.sum; r->pendingRaw=encoded.raw;
		}
		for(u32 i=0; i<encoded.len; i+=4)
		{
			u32 v=0;
			for(u32 k=0; k<4 && i+k<encoded.len; ++k) v|=(u32)p[i+k]<<(k*8);
			put(v);
		}
	}
	return 1;
}

int world_load_columns(u32 (*get)(void),u32 (*position)(void))
{
	if(terrainActive) terrain_reset();
	genSeed=get();
	genVersion=get();
	if(genVersion>1 || get()!=(u32)(g_NC*g_NC)) return 0;
	columnTop=0; preparedColumn=-1;
	cache_reset();
	int slot=0,pcx=CLAMP(loadX/CS,0,g_NC-1),pcz=CLAMP(loadZ/CS,0,g_NC-1);
	for(int id=0; id<g_NC*g_NC; ++id)
	{
		ColumnRecord *r=&columnRecords[id];
		r->len=get(); r->sum=get(); u32 raw=get();
		if(raw>1 || r->len>COLUMN_CELLS || (!position && r->len>columnCapacity-columnTop)) return 0;
		r->off=position ? position() : columnTop; r->raw=raw; r->disk=position!=NULL;
		u8 *dst=position ? columnScratch : columnArena+columnTop;
		if(!position) columnTop+=r->len;
		for(u32 i=0; i<r->len; i+=4)
		{
			u32 v=get();
			for(u32 k=0; k<4 && i+k<r->len; ++k) dst[i+k]=v>>(k*8);
		}
		if(position) preparedColumn=id;
		if(!column_valid(id%g_NC,id/g_NC)) return 0;
		if(ABS(id%g_NC-pcx)<=2 && ABS(id/g_NC-pcz)<=2)
		{
			cacheBoot=1; int ok=cache_load(id%g_NC,id/g_NC,slot++); cacheBoot=0;
			if(!ok) return 0;
		}
	}
	preparedColumn=-1; loadPosition=0;
	return 1;
}
