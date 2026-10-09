/* Temporary terrain backing, NOT a save format. Only the launcher-marked
   dedicated scratch disk may be written. Logical record ownership is RAM-only.
   Two sector-aligned slots per column preserve the last good record if a write
   fails. A small extra-RAM read cache avoids repeat SCSI commands on backtracking. */
#define TERRAIN_COLUMNS 256
#define TERRAIN_SECTORS (COLUMN_CELLS/512)
#define TERRAIN_DISK 2
#define TERRAIN_CACHE 32
u32 g_terrainDiskStats[4]; /* physical reads, writes, RAM hits, failed commands */
static u8 *terrainWriteBuffer,*terrainReadCache;
static short terrainTags[TERRAIN_CACHE];
static u32 terrainAges[TERRAIN_CACHE],terrainClock;
static u32 terrainVersions[TERRAIN_COLUMNS],terrainSavedVersions[TERRAIN_COLUMNS],terrainWriteVersion;
static int terrainWriteID=-1,terrainReadID=-1,terrainCacheCount;
static int terrainFloppyReadID=-1;
static ColumnRecord terrainPending;

static int terrain_init(void)
{
	static const char magic[]="TSC-TERRAIN-TEMP";
	terrainActive=0;
	if(!hdd_init() || hdd_sector_count()<1+TERRAIN_COLUMNS*TERRAIN_SECTORS*2) return 0;
	if(hdd_transfer_sync(0,1,0,columnScratch)!=1) return 0;
	if(memcmp(columnScratch,magic,16) || ((u32 *)columnScratch)[4]!=1 ||
	   ((u32 *)columnScratch)[5]!=TERRAIN_COLUMNS || ((u32 *)columnScratch)[6]!=TERRAIN_SECTORS) return 0;
	/* Reject unknown future header extensions as well. No automatic formatting. */
	for(int i=28; i<512; ++i) if(columnScratch[i]) return 0;
	terrainWriteBuffer=heap_alloc_high(COLUMN_CELLS);
	terrainCacheCount=0; terrainReadCache=NULL;
	terrainActive=1;
	terrain_reset();
	return 1;
}

static void terrain_cache_allocate(void)
{
	/* Permanent allocation only AFTER the temporary generation mark rewinds. */
	if(terrainActive && g_ramMB>=8 && !terrainReadCache)
	{
		terrainReadCache=heap_alloc_high(COLUMN_CELLS*TERRAIN_CACHE);
		terrainCacheCount=TERRAIN_CACHE;
	}
}

static void terrain_reset(void)
{
	hdd_cancel();
	terrainWriteID=terrainReadID=-1;
	memset(terrainTags,0xFF,sizeof(terrainTags));
	memset(terrainAges,0,sizeof(terrainAges));
	memset(g_terrainDiskStats,0,sizeof(g_terrainDiskStats));
	memset(terrainVersions,0,sizeof(terrainVersions));
	memset(terrainSavedVersions,0,sizeof(terrainSavedVersions));
	terrainClock=0;
}

static void terrain_cache_put(int id,const u8 *bytes,u32 len)
{
	if(!terrainCacheCount) return;
	int slot=0;
	for(int i=0; i<terrainCacheCount; ++i)
	{
		if(terrainTags[i]==id || terrainTags[i]<0) { slot=i; break; }
		if(terrainAges[i]<terrainAges[slot]) slot=i;
	}
	memcpy(terrainReadCache+slot*COLUMN_CELLS,bytes,len);
	terrainTags[slot]=id; terrainAges[slot]=++terrainClock;
}

static int terrain_drain_floppy(void)
{
	if(terrainFloppyReadID<0) return 1;
	ColumnRecord *old=&columnRecords[terrainFloppyReadID];
	int status=columnRead ? columnRead(old->off,old->len,columnScratch) : -1;
	if(status) { terrainFloppyReadID=-1; preparedColumn=-1; }
	return status;
}

static int column_read_record(const void *record)
{
	const ColumnRecord *r=record;
	int id=(int)(r-columnRecords);
	/* The floppy driver masks all DMA channels when a track finishes. Do not
	   overlap it with SCSI DMA even when travel changes the requested backend. */
	if(terrainFloppyReadID>=0 && terrainFloppyReadID!=id)
	{
		int status=terrain_drain_floppy();
		if(status<=0) return status;
	}
	if(r->disk!=TERRAIN_DISK)
	{
		if(terrainReadID>=0)
		{
			int status=column_read_record(&columnRecords[terrainReadID]);
			if(status<=0) return status;
			preparedColumn=-1;
		}
		int status=columnRead ? columnRead(r->off,r->len,columnScratch) : -1;
		terrainFloppyReadID=status==0 ? id : -1;
		return status;
	}
	/* Drain a previously requested read even if the camera changes direction.
	   Never let two commands own the SCSI controller/buffer simultaneously. */
	if(terrainWriteID>=0) return 0;
	if(terrainReadID>=0 && terrainReadID!=id)
	{
		ColumnRecord *old=&columnRecords[terrainReadID];
		int status=terrainBlocking ? hdd_transfer_sync(old->off/512,(old->len+511)/512,0,columnScratch) :
		                            hdd_transfer(old->off/512,(old->len+511)/512,0,columnScratch);
		if(!status) return 0;
		if(status>0) terrain_cache_put(terrainReadID,columnScratch,old->len);
		terrainReadID=-1;
		if(status<0) { ++g_terrainDiskStats[3]; return -1; }
	}
	/* Even a RAM hit must wait for the previous DMA/copy to release scratch. */
	for(int i=0; i<terrainCacheCount; ++i)
		if(terrainTags[i]==id)
		{
			memcpy(columnScratch,terrainReadCache+i*COLUMN_CELLS,r->len);
			terrainAges[i]=++terrainClock; ++g_terrainDiskStats[2]; return 1;
		}
	if(terrainReadID<0) { terrainReadID=id; ++g_terrainDiskStats[0]; }
	int status=terrainBlocking ? hdd_transfer_sync(r->off/512,(r->len+511)/512,0,columnScratch) :
	                            hdd_transfer(r->off/512,(r->len+511)/512,0,columnScratch);
	if(status)
	{
		terrainReadID=-1;
		if(status>0) terrain_cache_put(id,columnScratch,r->len);
		else ++g_terrainDiskStats[3];
	}
	return status;
}

static int terrain_finish_write(void)
{
	if(terrainWriteID<0) return 1;
	int status=terrainBlocking ? hdd_transfer_sync(terrainPending.off/512,(terrainPending.len+511)/512,1,terrainWriteBuffer) :
	                            hdd_transfer(terrainPending.off/512,(terrainPending.len+511)/512,1,terrainWriteBuffer);
	if(!status) return 0;
	if(status<0) { ++g_terrainDiskStats[3]; terrainWriteID=-1; streamError=2; return -1; }
	columnRecords[terrainWriteID]=terrainPending;
	terrainSavedVersions[terrainWriteID]=terrainWriteVersion;
	terrain_cache_put(terrainWriteID,terrainWriteBuffer,terrainPending.len);
	terrainWriteID=-1;
	return 1;
}

/* Explicit save/export/startup may block; traversal never calls this. */
static int terrain_quiesce(void)
{
	terrainBlocking=1;
	if(terrainFloppyReadID>=0)
	{
		int status;
		do { status=terrain_drain_floppy(); } while(!status);
		if(status<0) { terrainBlocking=0; return 0; }
	}
	if(terrain_finish_write()<0) { terrainBlocking=0; return 0; }
	if(terrainReadID>=0)
	{
		ColumnRecord *r=&columnRecords[terrainReadID];
		int status=hdd_transfer_sync(r->off/512,(r->len+511)/512,0,columnScratch);
		if(status>0) terrain_cache_put(terrainReadID,columnScratch,r->len);
		terrainReadID=-1;
		if(status<0) { terrainBlocking=0; return 0; }
	}
	preparedColumn=-1;
	terrainBlocking=0;
	return 1;
}

static int terrain_capture(int cx,int cz)
{
	int id=cz*g_NC+cx;
	if(terrainWriteID>=0)
	{
		int status=terrain_finish_write();
		if(status<=0) return 0;
	}
	if(terrainReadID>=0) return 0;
	int floppyStatus;
	do { floppyStatus=terrain_drain_floppy(); } while(terrainBlocking && !floppyStatus);
	if(floppyStatus<=0) { if(floppyStatus<0) streamError=2; return 0; }
	ColumnRecord *r=&columnRecords[id];
	/* A completed background write may precede this eviction attempt. Compare
	   exact edit revisions, not checksum equality: in-flight edits must survive. */
	if(r->disk==TERRAIN_DISK && terrainSavedVersions[id]==terrainVersions[id]) return 1;
	memset(&terrainPending,0,sizeof(terrainPending));
	column_encode(cx,cz,terrainWriteBuffer,&terrainPending);
	terrainWriteVersion=terrainVersions[id];
	/* Alternate fixed slots. The directory changes only after a good status. */
	u32 first=(1+id*TERRAIN_SECTORS*2)*512u;
	terrainPending.off=r->disk==TERRAIN_DISK && r->off==first ? first+COLUMN_CELLS : first;
	terrainPending.disk=TERRAIN_DISK;
	memset(terrainWriteBuffer+terrainPending.len,0,((terrainPending.len+511)&~511u)-terrainPending.len);
	terrainWriteID=id; ++g_terrainDiskStats[1];
	return terrain_finish_write()>0;
}

int world_hdd_active(void) { return terrainActive; }
int world_backing_idle(void) { return terrain_quiesce(); }
