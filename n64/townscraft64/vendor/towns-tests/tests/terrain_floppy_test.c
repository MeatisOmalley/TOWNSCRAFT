/* Actual save/load and floppy DMA fixture with mixed temporary HDD backing.
   Link with blocks.c, fmath.c, libc.c and generated tables.c, without no_hdd.c. */
#define COLUMN_SAVE_FIXTURE
#include "column_save_test.c"

static int floppyReadsAtStart,floppyWritesAtStart,pendingPolls;
#define HDD_MOCK_ON_START() do { \
	floppyReadsAtStart=readCount; floppyWritesAtStart=writeCount; \
} while(0)
#define HDD_MOCK_ON_POLL() do { \
	++pendingPolls; \
	check(readCount==floppyReadsAtStart && writeCount==floppyWritesAtStart, \
	      "floppy transfers wait until outstanding HDD command completes"); \
} while(0)
#include "hdd_mock.inc"

static u8 protectedBank[BANK_TRACKS*TRACK_BYTES];

static void settle_mixed(int x,int z)
{
	cacheLastX=cacheLastZ=-1; cacheDirectionX=cacheDirectionZ=0;
	for(int tick=0; tick<6000; ++tick)
	{
		world_update_dirty_chunks(1); world_stream(x,z,20,1); save_stream_tick();
		int ready=cacheLightSlot<0 && terrainReadID<0 && terrainWriteID<0 && !mockBusy;
		for(int cz=MAX(0,z/CS-2); cz<=MIN(g_NC-1,z/CS+2); ++cz)
		for(int cx=MAX(0,x/CS-2); cx<=MIN(g_NC-1,x/CS+2); ++cx)
			if(!world_column_sim_ready(cx,cz)) ready=0;
		if(ready) return;
	}
	check(0,"mixed HDD/floppy travel eventually settles");
}

static void check_idle(void)
{
	check(!mockBusy && terrainReadID<0 && terrainWriteID<0,
	      "save/load releases outstanding HDD request and buffer ownership");
}

static void fail_pending_save(const char *message)
{
	int reads=readCount,writes=writeCount;
	mockFail=1;
	check(save_world()==SAVE_DISK_ERROR,message);
	check_idle();
	check(readCount==reads && writeCount==writes,
	      "failed HDD quiescence returns before any floppy transfer");
}

static void check_saved_edit(void)
{
	check(wget(8,40,8)==B_GLASS && wget(9,40,8)==B_TORCH,
	      "actual floppy load restores latest block and torch");
	check((world_light_at(10,40,8)&15)==13,
	      "mixed backing restores exact adjacent torch light");
}

int main(void)
{
	g_ramMB=4;
	reference_world(160,4242);
	memcpy(testDisk,"TSC-TERRAIN-TEMP",16);
	((u32 *)testDisk)[4]=1; ((u32 *)testDisk)[5]=256; ((u32 *)testDisk)[6]=24;
	world_alloc(); world_generate(4242);
	check(world_hdd_active() && g_W==160 && columnTop==0,
	      "160-wide generated world starts entirely HDD backed");
	check(g_terrainDiskStats[1]==100,"all 100 generated columns reach HDD");
	mobs_clear(); memset(regionVisited,1,sizeof(regionVisited));
	g_player.body.x=g_player.body.z=8*FU; g_player.body.y=40*FU;
	settle_mixed(8,8);
	world_set(8,40,8,B_PLANKS); world_set(9,40,8,B_TORCH);
	g_inv[0].item=B_LOG; g_inv[0].count=9;

	preparedColumn=-1; terrainBlocking=0;
	check(!column_prepare(9,9) && mockBusy,"prime failing read at save entry");
	fail_pending_save("pending HDD read failure aborts actual floppy save");
	check(wget(8,40,8)==B_PLANKS && wget(9,40,8)==B_TORCH,
	      "failed read at save entry preserves resident edits");

	/* PF9 must finish a traversal read before its first floppy transfer. */
	preparedColumn=-1; terrainBlocking=0;
	check(!column_prepare(9,9) && mockBusy && terrainReadID==99,
	      "first actual save starts with a pending HDD read");
	int polls=pendingPolls;
	check(save_world()==SAVE_OK,"HDD terrain saves through actual version-4 floppy path");
	check_idle();
	check(pendingPolls>polls,"save polls the outstanding HDD read to completion");
	int oldBank=world_backing_bank(BANK_TRACKS*TRACK_BYTES);
	check(oldBank==0 || oldBank==1,"successful save publishes a floppy source bank");
	check(((u32 *)(disk+oldBank*BANK_TRACKS*TRACK_BYTES))[1]==4,
	      "160-wide actual save uses version 4");
	for(int id=0; id<g_NC*g_NC; ++id)
		check(columnRecords[id].disk==1,"successful save commits every record to floppy");
	memcpy(protectedBank,disk+oldBank*BANK_TRACKS*TRACK_BYTES,sizeof(protectedBank));
	printf("PASS: actual v4 save quiesces pending HDD read and commits floppy backing\n");

	/* Dirty eviction creates HDD records while clean records stay on floppy. */
	world_set(8,40,8,B_STONE);
	settle_mixed(148,148);
	check(!world_column_ready(0,0) && columnRecords[0].disk==TERRAIN_DISK,
	      "dirty column evicts to HDD after a committed floppy save");
	int hddRecords=0,floppyRecords=0;
	for(int id=0; id<g_NC*g_NC; ++id)
	{
		hddRecords+=columnRecords[id].disk==TERRAIN_DISK;
		floppyRecords+=columnRecords[id].disk==1;
	}
	check(hddRecords>0 && floppyRecords>0,"world has both HDD and floppy record owners");
	check(world_backing_bank(BANK_TRACKS*TRACK_BYTES)==oldBank,
	      "HDD offsets cannot select the protected floppy bank");
	settle_mixed(8,8);
	check(wget(8,40,8)==B_STONE && wget(9,40,8)==B_TORCH,
	      "mixed backing round trip preserves evicted edits");

	/* A failed write must not touch the protected floppy snapshot. */
	ColumnRecord oldRecord=columnRecords[0];
	world_set(8,40,8,B_PLANKS); terrainBlocking=0;
	check(!column_capture(0,0) && mockBusy,"prime failing write at save entry");
	world_set(8,40,8,B_GLASS);
	fail_pending_save("pending HDD write failure aborts actual floppy save");
	check(columnRecords[0].off==oldRecord.off && columnRecords[0].sum==oldRecord.sum &&
	      residentDirty[g_columnMap[0]] && wget(8,40,8)==B_GLASS,
	      "failed pending write retains previous record and newer resident edit");
	check(!memcmp(protectedBank,disk+oldBank*BANK_TRACKS*TRACK_BYTES,sizeof(protectedBank)),
	      "failed quiescence leaves committed floppy source bank intact");

	/* A newer edit during a pending write must win in the explicit snapshot. */
	world_set(8,40,8,B_PLANKS); terrainBlocking=0;
	check(!column_capture(0,0) && mockBusy && terrainWriteID==0,
	      "second actual save starts with a pending HDD write");
	world_set(8,40,8,B_GLASS);
	polls=pendingPolls;
	check(save_world()==SAVE_OK,"mixed source saves with an in-flight older block write");
	check_idle();
	check(pendingPolls>polls,"save polls the outstanding HDD write to completion");
	check(!memcmp(protectedBank,disk+oldBank*BANK_TRACKS*TRACK_BYTES,sizeof(protectedBank)),
	      "second snapshot leaves the entire old source bank byte-exact");
	check(world_backing_bank(BANK_TRACKS*TRACK_BYTES)==1-oldBank,
	      "second snapshot publishes the other bank");
	printf("PASS: mixed HDD/floppy save protects source bank and includes edit during write\n");

	/* Loading must likewise release HDD ownership before touching floppy DMA. */
	world_set(8,40,8,B_STONE); terrainBlocking=1;
	check(column_capture(0,0),"prepare a temporary HDD record before reload");
	preparedColumn=-1; terrainBlocking=0;
	check(!column_prepare(0,0) && mockBusy,"actual load starts with a pending HDD read");
	check(load_world()==SAVE_OK,"newest committed mixed snapshot loads through actual driver");
	check_idle(); world_rebuild_after_load(); check_saved_edit();
	check(g_inv[0].item==B_LOG && g_inv[0].count==9,"inventory survives actual mixed snapshot");
	settle_mixed(148,148); settle_mixed(8,8); check_saved_edit();
	printf("PASS: actual mixed snapshot reload and subsequent floppy paging restore blocks/torch/light\n");
	return 0;
}
