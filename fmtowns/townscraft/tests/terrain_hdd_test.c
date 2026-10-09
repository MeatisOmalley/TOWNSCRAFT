/* World/lighting equivalence and failure recovery with delayed SCSI commands. */
#define COLUMN_CACHE_FIXTURE
#include "column_cache_test.c"
static u8 testDisk[(1+TERRAIN_COLUMNS*TERRAIN_SECTORS*2)*512];
static int mockPresent=1,mockBusy,mockDelay,mockWrite,mockFail,mockCalls;
static u32 mockLBA,mockCount;
static u8 *mockBuffer;
int hdd_init(void) { return mockPresent; }
u32 hdd_sector_count(void) { return sizeof(testDisk)/512; }
void hdd_cancel(void) { mockBusy=0; }
int hdd_transfer(u32 lba,u32 count,int write,u8 *buffer)
{
	++mockCalls;
	check(lba+count<=sizeof(testDisk)/512,"SCSI request stays in scratch area");
	if(!mockBusy)
	{
		mockLBA=lba; mockCount=count; mockWrite=write; mockBuffer=buffer;
		mockDelay=3; mockBusy=1;
	}
	check(mockLBA==lba && mockCount==count && mockWrite==write && mockBuffer==buffer,
	      "pending command retains request and buffer ownership");
	if(mockDelay-->0) return 0;
	mockBusy=0;
	if(mockFail)
	{
		mockFail=0;
		if(write) memcpy(testDisk+lba*512,buffer,32); /* failed partial write */
		return -1;
	}
	if(write) memcpy(testDisk+lba*512,buffer,count*512);
	else memcpy(buffer,testDisk+lba*512,count*512);
	return 1;
}
int hdd_transfer_sync(u32 lba,u32 count,int write,u8 *buffer)
{
	int status;
	do { status=hdd_transfer(lba,count,write,buffer); } while(!status);
	return status;
}
static void settle(int x,int z)
{
	cacheLastX=cacheLastZ=-1; cacheDirectionX=cacheDirectionZ=0;
	for(int tick=0; tick<4000; ++tick)
	{
		world_update_dirty_chunks(1); world_stream(x,z,20,1);
		int ready=cacheLightSlot<0 && terrainWriteID<0 && terrainReadID<0;
		for(int cz=MAX(0,z/CS-2); cz<=MIN(g_NC-1,z/CS+2); ++cz)
		for(int cx=MAX(0,x/CS-2); cx<=MIN(g_NC-1,x/CS+2); ++cx)
			if(!world_column_sim_ready(cx,cz)) ready=0;
		if(ready) return;
	}
	check(0,"cooperative HDD travel eventually settles");
}
int main(void)
{
	g_ramMB=8;
	world_alloc();
	check(!world_hdd_active(),"unmarked disk is never claimed by guest");
	check(!testDisk[0],"probing unknown disk performs no writes");
	memcpy(testDisk,"TSC-TERRAIN-TEMP",16);
	((u32 *)testDisk)[4]=1; ((u32 *)testDisk)[5]=256; ((u32 *)testDisk)[6]=24;
	reference_world(256,4242);
	world_alloc(); world_generate(4242);
	check(world_hdd_active() && columnTop==0,"new unsaved world is HDD backed, not floppy or RAM archive");
	check(g_terrainDiskStats[1]==256,"every generated column was written to HDD");
	check(terrainCacheCount==32,"8 MB profile uses a bounded 384 KiB read cache");
	compare_resident(g_spawnX,g_spawnZ);
	settle(8,8); compare_resident(8,8);
	world_set(8,40,8,B_PLANKS); world_set(9,40,8,B_TORCH);
	settle(248,248);
	check(!world_column_ready(0,0),"edited column actually evicts");
	settle(8,8);
	check(wget(8,40,8)==B_PLANKS && wget(9,40,8)==B_TORCH && world_light_at(10,40,8)%16==13,
	      "HDD eviction preserves blocks and relights torches");
	/* Backtracking reads should hit RAM without issuing another controller command. */
	int id=0; ColumnRecord *r=&columnRecords[id];
	preparedColumn=-1;
	u32 before=g_terrainDiskStats[0],hits=g_terrainDiskStats[2];
	check(column_prepare(0,0) && g_terrainDiskStats[0]==before && g_terrainDiskStats[2]>hits,
	      "read cache eliminates repeat SCSI command");
	/* A failed write uses the alternate slot and keeps old directory/dirty owner. */
	world_set(8,40,8,B_STONE);
	ColumnRecord previous=*r;
	terrainBlocking=0; mockFail=1;
	check(!column_capture(0,0),"traversal write yields while command is pending");
	while(terrainWriteID>=0) terrain_finish_write();
	check(r->off==previous.off && r->sum==previous.sum && residentDirty[g_columnMap[0]] && wget(8,40,8)==B_STONE,
	      "partial write failure keeps old record and resident edits");
	check(column_valid_bytes(&previous,testDisk+previous.off),"failed alternate write leaves old HDD record intact");
	/* Editing during an in-flight write requires another write before eviction. */
	check(!column_capture(0,0),"retry begins a cooperative write");
	world_set(8,40,8,B_GLASS);
	while(terrainWriteID>=0) terrain_finish_write();
	check(!column_capture(0,0),"newer edit is not mistaken for completed older write");
	while(terrainWriteID>=0) terrain_finish_write();
	check(column_capture(0,0),"latest edit reaches backing before release");
	settle(248,248); settle(8,8);
	check(wget(8,40,8)==B_GLASS,"edit during write survives round trip");
	/* Reverse direction while a physical read is pending. */
	memset(terrainTags,0xFF,sizeof(terrainTags)); preparedColumn=-1;
	terrain_cache_put(0,testDisk+columnRecords[0].off,columnRecords[0].len);
	check(!column_prepare(15,15) && terrainReadID==255,"read starts without blocking gameplay");
	for(int i=0; i<20 && !column_prepare(0,0); ++i) {}
	check(preparedColumn==0 && terrainReadID<0 && !mockBusy,
	      "reversal to a RAM hit drains physical read before sharing scratch");
	preparedColumn=-1;
	memset(terrainTags,0xFF,sizeof(terrainTags));
	check(!column_prepare(15,15) && terrainReadID==255,"second reversal starts a physical read");
	for(int i=0; i<20 && !column_prepare(14,15); ++i) {}
	check(preparedColumn==254 && terrainReadID<0,"camera reversal drains previous read safely");
	/* Export remains byte-exact with HDD records and does not redirect to floppy. */
	serialPos=0; check(world_save_columns(put_word,NULL),"HDD-backed terrain exports through existing record format");
	check(serialPos<ARRAY_LEN(serialized),"serialized fixture stays bounded");
	world_set(8,40,8,B_AIR);
	serialPos=0; world_load_position(8,8);
	check(world_load_columns(get_word,NULL),"export imports without requiring HDD persistence");
	world_rebuild_after_load();
	check(wget(8,40,8)==B_GLASS,"exported terrain includes latest edit");
	world_generate(42);
	check(world_hdd_active() && g_terrainDiskStats[1]==256,"new world invalidates old record/cache ownership");
	printf("PASS: HDD terrain equivalence, async reads/writes, RAM hits, partial failures, edits in flight, reversal, export and repeated generation\n");
	return 0;
}
