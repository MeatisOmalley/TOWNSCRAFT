/* 1024-block HDD world: immutable base, edit slots (V1/V2) or an edit log
 * (V3), compact save index. Included after column_cache.inc by the opt-in DOS
 * large-world overlay. */
static u32 large_column_offset(int id,int slot)
{
	if(terrainV2)
		return slot ? (terrainPoolSector+((u32)terrainPair[id]*2u+(u32)slot-1u)*TERRAIN_SECTORS)*512u :
		              terrainBaseOff[id];
	return (33u+(u32)id*TERRAIN_SECTORS*3u+(u32)slot*TERRAIN_SECTORS)*512u;
}
/* Slot (0 base, 1/2 edit) a record of column id occupies. */
static int large_column_slot(int id,u32 off)
{
	if(!terrainV2) return (off-large_column_offset(id,0))/COLUMN_CELLS;
	if(off==terrainBaseOff[id]) return 0;
	return (off-terrainPoolSector*512u)/COLUMN_CELLS%2u+1;
}
/* V2: give column id an edit slot pair on its first edit (lowest free). */
static int large_pool_take(int id,u32 pair)
{
	if(pair>=terrainPoolPairs || (terrainPairUsed[pair>>3]&(1<<(pair&7)))) return 0;
	terrainPairUsed[pair>>3]|=1<<(pair&7);
	terrainPair[id]=pair;
	return 1;
}
static int large_pool_assign(int id)
{
	if(terrainPair[id]!=0xFFFF) return 1;
	for(u32 pair=0; pair<terrainPoolPairs; ++pair)
		if(large_pool_take(id,pair)) return 1;
	return 0;
}

/* V3 edit log. A record is one column in the column codec, sector aligned,
   anywhere in the log; an extent is written sector<<5|count (absolute).
   Space is a bitmap that is never smaller than the truth: it covers the live
   directory, both committed banks' records (logPin) and a write in flight.
   Allocation marks bits; only an exact rebuild (after a commit or load, or
   before giving up) releases records that nothing references any more. A
   record referenced by either committed bank is therefore never overwritten. */
#define LOG_MAX_SECTORS 65536u
#define LOG_WINDOWS 16u           /* compaction works on 1/16 of the log */
static u32 logBits[LOG_MAX_SECTORS/32];
static u32 logPin[2][TERRAIN_COLUMNS];  /* newest, older committed bank; 0 = base */
static u32 logHead,logFree;             /* next-fit cursor; free sectors (lower bound) */
static int logExact;
/* rebuilds, head wraps, compactions, moved records, moved sectors, compaction ticks,
   refused play-time records */
u32 g_terrainLogStats[7];
int g_terrainLogForceCompact;

static u32 large_ref(const ColumnRecord *r)
{
	u32 s=r->off/512u;
	if(!terrainV3 || r->disk!=TERRAIN_DISK || r->off%512u || s<terrainLogFirst || s>=terrainLogEnd)
		return 0;
	return s<<5 | (r->len+511u)/512u;
}
static int log_valid_ref(u32 ref)
{
	u32 s=ref>>5,n=ref&31u;
	return n && n<=TERRAIN_SECTORS && s>=terrainLogFirst && s<terrainLogEnd && n<=terrainLogEnd-s;
}
static int log_bit(u32 i) { return (logBits[i>>5]>>(i&31))&1u; }
/* Mark an extent; 0 if part of it was already marked. */
static int log_mark(u32 ref)
{
	int clear=1;
	for(u32 i=(ref>>5)-terrainLogFirst,end=i+(ref&31u); i<end; ++i)
	{
		if(log_bit(i)) clear=0;
		logBits[i>>5]|=1u<<(i&31);
	}
	return clear;
}
static void log_unmark(u32 at,u32 n)
{
	for(u32 i=at; i<at+n; ++i) logBits[i>>5]&=~(1u<<(i&31));
}
/* Exact bitmap from every reference; 0 if two live records overlap. */
static int log_rebuild(void)
{
	u32 size=terrainLogEnd-terrainLogFirst;
	int ok=1;
	memset(logBits,0,sizeof(logBits));
	for(int id=0; id<TERRAIN_COLUMNS; ++id)
	{
		u32 ref=large_ref(&columnRecords[id]);
		if(ref && !log_mark(ref)) ok=0;
	}
	for(int b=0; b<2; ++b)
		for(int id=0; id<TERRAIN_COLUMNS; ++id)
			if(logPin[b][id]) log_mark(logPin[b][id]);
	if(terrainWriteID>=0 && large_ref(&terrainPending)) log_mark(large_ref(&terrainPending));
	logFree=0;
	for(u32 i=0; i<size; ++i)
	{
		u32 w=logBits[i>>5];
		if(!(i&31) && i+32<=size && (!w || w==~0u)) { if(!w) logFree+=32; i+=31; }
		else logFree+=!((w>>(i&31))&1u);
	}
	logExact=1; ++g_terrainLogStats[0];
	return ok;
}
static void large_log_reset(void)
{
	memset(logPin,0,sizeof(logPin));
	memset(logBits,0,sizeof(logBits));
	logHead=0; logFree=terrainLogEnd-terrainLogFirst; logExact=1;
}
/* First run of n free sectors in [from,end), outside [skip0,skip1); ~0u if none. */
static u32 log_find(u32 from,u32 end,u32 n,u32 skip0,u32 skip1)
{
	u32 run=0;
	for(u32 i=from; i<end; ++i)
	{
		u32 w=logBits[i>>5];
		if(i>=skip0 && i<skip1) { run=0; i=skip1-1; continue; }
		if(!(i&31) && i+32<=end && (i+32<=skip0 || i>=skip1) && (!w || w==~0u))
		{
			if(w) run=0;
			else if(run+32>=n) return i-run;
			else run+=32;
			i+=31; continue;
		}
		if((w>>(i&31))&1u) run=0;
		else if(++run==n) return i+1-n;
	}
	return ~0u;
}
/* Free runs as whole 24-sector records: what a flush of dirty columns needs. */
static u32 log_slots(void)
{
	u32 size=terrainLogEnd-terrainLogFirst,slots=0,run=0;
	for(u32 i=0; i<size; ++i)
	{
		u32 w=logBits[i>>5];
		if(!(i&31) && i+32<=size && (!w || w==~0u))
		{
			if(w) { slots+=run/TERRAIN_SECTORS; run=0; } else run+=32;
			i+=31; continue;
		}
		if((w>>(i&31))&1u) { slots+=run/TERRAIN_SECTORS; run=0; } else ++run;
	}
	return slots+run/TERRAIN_SECTORS;
}
/* Place a record of len bytes for column id. Play-time records (non-blocking
   eviction) must leave room for a save to flush every resident column; saves,
   loads and other blocking captures may use that reserve. */
static int large_log_alloc(int id,u32 len,u32 *off)
{
	u32 size=terrainLogEnd-terrainLogFirst,n=(len+511u)/512u,at=~0u;
	for(;;)
	{
		at=log_find(logHead,size,n,0,0);
		if(at==~0u && (at=log_find(0,size,n,0,0))!=~0u) ++g_terrainLogStats[1];
		if(at!=~0u)
		{
			log_mark((terrainLogFirst+at)<<5|n);
			if(terrainBlocking || log_slots()>=(u32)g_residentColumns+1) break;
			log_unmark(at,n); at=~0u;
		}
		if(logExact) break;
		log_rebuild();
	}
	if(at==~0u) { ++g_terrainLogStats[6]; return 0; }
	/* The record it replaces is unreferenced unless a bank pins it. */
	if(large_ref(&columnRecords[id])) logExact=0;
	logFree-=n; logHead=at+n;
	*off=(terrainLogFirst+at)*512u;
	return 1;
}
/* Save-time defragmentation, bounded by one window: 1/16 of the log, so at
   most 1 MiB read and 1 MiB written for a 16 MiB log. When too few 24-sector
   runs remain, the live records in the window holding the least live data
   are copied into holes elsewhere and the directory points at the copies.
   The originals stay pinned by the banks; once the next save commits, the
   window is one free run. Best effort: a failed copy keeps its original. */
static void large_log_compact(void)
{
	u32 size=terrainLogEnd-terrainLogFirst,window=(size+LOG_WINDOWS-1)/LOG_WINDOWS;
	u32 live[LOG_WINDOWS],best=0,t0=g_ticks;
	log_rebuild();
	if(log_slots()>=2u*((u32)g_residentColumns+1) && !g_terrainLogForceCompact) return;
	memset(live,0,sizeof(live));
	for(int id=0; id<TERRAIN_COLUMNS; ++id)
	{
		u32 ref=large_ref(&columnRecords[id]),s=(ref>>5)-terrainLogFirst;
		if(!ref) continue;
		for(u32 w=s/window; w<=(s+(ref&31u)-1)/window; ++w) live[w]+=ref&31u;
	}
	/* A window without live records frees itself once the banks move on. */
	for(u32 w=0; w<LOG_WINDOWS; ++w) if(live[w] && (!live[best] || live[w]<live[best])) best=w;
	if(!live[best]) return;
	u32 w0=best*window,w1=MIN(size,w0+window);
	terrainBlocking=1;
	for(int id=0; id<TERRAIN_COLUMNS; ++id)
	{
		ColumnRecord *r=&columnRecords[id];
		u32 ref=large_ref(r),s=(ref>>5)-terrainLogFirst,n=ref&31u,at;
		if(!ref || s>=w1 || s+n<=w0) continue;
		if((at=log_find(0,size,n,w0,w1))==~0u) break;
		if(hdd_transfer_sync(r->off/512u,n,0,columnScratch)!=1 ||
		   column_checksum(columnScratch,r->len)!=r->sum) continue;
		log_mark((terrainLogFirst+at)<<5|n);
		if(hdd_transfer_sync(terrainLogFirst+at,n,1,columnScratch)!=1) continue;
		r->off=(terrainLogFirst+at)*512u;
		++g_terrainLogStats[3]; g_terrainLogStats[4]+=n;
	}
	terrainBlocking=0; preparedColumn=-1;
	logHead=0; ++g_terrainLogStats[2]; g_terrainLogStats[5]=g_ticks-t0;
	log_rebuild();
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
		r->len=terrainV2 ? terrainBaseLen[id] : COLUMN_CELLS; r->sum=terrainBaseSum[id];
		r->raw=r->len==COLUMN_CELLS; r->disk=TERRAIN_DISK;
	}
	g_spawnX=terrainSpawnX; g_spawnZ=terrainSpawnZ;
#ifdef DOS_FAR_FIELD
    far_world_reset();
#endif
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
	if(terrainV3) large_log_compact();
	put(terrainSeed);
	put(column_checksum((const u8 *)terrainBaseSum,sizeof(terrainBaseSum)));
	put(TERRAIN_COLUMNS);
	for(int id=0; id<TERRAIN_COLUMNS; ++id)
	{
		const ColumnRecord *r=&columnRecords[id];
		u32 first=large_column_offset(id,0),slot;
		if(terrainV3)
		{
			/* Record offset (0 = base), length, sum, and the record the
			   last committed save holds, which becomes the older bank. */
			u32 ref=large_ref(r);
			if(r->disk!=TERRAIN_DISK || !r->len || r->len>COLUMN_CELLS ||
			   r->raw!=(r->len==COLUMN_CELLS) || (!ref && r->off!=first))
				return 0;
			put(ref ? r->off : 0);
			put(r->len); put(r->sum); put(logPin[0][id]);
			continue;
		}
		if(terrainV2)
		{
			/* Edited columns also record their pool pair (bits 2 up). */
			slot=large_column_slot(id,r->off);
			if(r->disk!=TERRAIN_DISK || !r->len || r->len>COLUMN_CELLS || r->raw>1 ||
			   (slot && (terrainPair[id]==0xFFFF || r->off!=large_column_offset(id,slot))))
				return 0;
			put(slot|(slot ? (u32)terrainPair[id]<<2 : 0));
			put(r->len); put(r->sum); put(r->raw);
			continue;
		}
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
		u32 field=get(),len=get(),sum=get(),raw=get();
		u32 slot=terrainV2 ? (field&3u) : field;
		ColumnRecord *r=&columnRecords[id];
		if(terrainV3)
		{
			/* raw holds the older bank's record; both banks stay pinned. */
			u32 ref=field ? field/512u<<5 | (len+511u)/512u : 0;
			if(!len || len>COLUMN_CELLS || (raw && !log_valid_ref(raw)) ||
			   (field ? field%512u || !log_valid_ref(ref) :
			            len!=terrainBaseLen[id] || sum!=terrainBaseSum[id]))
				return 0;
			memset(r,0,sizeof(*r));
			r->off=field ? field : terrainBaseOff[id];
			r->len=len; r->sum=sum; r->raw=len==COLUMN_CELLS; r->disk=TERRAIN_DISK;
			logPin[0][id]=ref; logPin[1][id]=raw;
#ifdef DOS_STREAM_LIGHT_CACHE
            if(field) stream_light_invalidate((id%g_NC)*CS,(id/g_NC)*CS);
#endif
			continue;
		}
		if(slot>2 || !len || len>COLUMN_CELLS || raw>1 ||
		   (raw && len!=COLUMN_CELLS))
			return 0;
		if(terrainV2 ? (slot==0 && (field!=0 || len!=terrainBaseLen[id] ||
		                            raw!=(len==COLUMN_CELLS) || sum!=terrainBaseSum[id])) ||
		               (slot && !large_pool_take(id,field>>2)) :
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
	/* Distinct live records never share sectors; that would be corrupt. */
	if(terrainV3 && !log_rebuild()) return 0;
#ifdef DOS_FAR_FIELD
    far_world_reset();
#endif
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
	if(terrainV3)
	{
		/* The new bank is newest; the previous newest is now the older one.
		   Records only the overwritten bank referenced become free. */
		for(int id=0; id<TERRAIN_COLUMNS; ++id)
		{
			logPin[1][id]=logPin[0][id];
			logPin[0][id]=large_ref(&columnRecords[id]);
		}
		log_rebuild();
		return;
	}
	for(int id=0; id<TERRAIN_COLUMNS; ++id)
		terrainPinSlot[id]=large_column_slot(id,columnRecords[id].off);
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
