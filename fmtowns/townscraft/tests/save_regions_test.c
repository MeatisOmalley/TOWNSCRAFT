/* Exercise the actual versioned save stream through a fake DMA floppy. */
#define main spawn_regressions_main
#include "mobs_test.c"
#undef main
#include "../src/save.h"
#include "../src/game.h"
Slot g_inv[INV_SLOTS];
Chest g_chests[MAX_CHESTS];
int g_spawnX=48,g_spawnY=1,g_spawnZ=48,g_time=1000;
void (*g_genProgress)(int);
void chests_clear(void) { memset(g_chests,0,sizeof(g_chests)); }
void inv_clear(void) { memset(g_inv,0,sizeof(g_inv)); }
int world_cache_active(void) { return 0; }
int world_store_flush(void) { return 1; }
int world_save_columns(void (*put)(u32),u32 (*position)(void)) { return 0; }
int world_load_columns(u32 (*get)(void),u32 (*position)(void)) { return 0; }
int world_backing_bank(u32 bankBytes) { return -1; }
int world_materialize_columns(void) { return 1; }
int world_save_legacy_columns(void (*put)(int)) { return 0; }
void world_commit_columns(void) {}
void world_set_backing_reader(int (*read)(u32,u32,u8 *)) {}
void world_load_position(int x,int z) {}
void world_import_begin(void) {}
int world_import_end(int ok,int x,int z) { return ok; }
static u8 disk[1232*1024];
static u32 clockTicks,dmaAddress;
static int diskCyl,targetCyl,diskSide,diskStatus;
static u32 test_ticks(void) { return clockTicks++; }
static void test_outb(u16 port,u8 v);
static u8 test_inb(u16 port) { return port==0x200 ? diskStatus : 0; }
#define g_ticks test_ticks()
#define outb test_outb
#define inb test_inb
#include "../src/save.c"
#undef g_ticks
#undef outb
#undef inb
static void test_outb(u16 port,u8 v)
{
	if(port>=0xA4 && port<=0xA7)
	{
		int sh=(port-0xA4)*8;
		dmaAddress=(dmaAddress&~(255u<<sh))|((u32)v<<sh);
	}
	if(port==0x208) diskSide=(v>>2)&1;
	if(port==0x206) targetCyl=v;
	if(port!=0x200) return;
	if(v==0) diskCyl=0;
	if(v==0x10) diskCyl=targetCyl;
	if(v==0x90 || v==0xB0)
	{
		int track=diskCyl*2+diskSide;
		check(track<NUM_TRACKS && dmaAddress==(u32)trackBuf,"valid DMA track transfer");
		check((dmaAddress&65535)+TRACK_BYTES<=65536,"DMA stays inside segment");
		if(v==0x90) memcpy(trackBuf,disk+track*TRACK_BYTES,TRACK_BYTES);
		else memcpy(disk+track*TRACK_BYTES,trackBuf,TRACK_BYTES);
	}
}
static void legacy_save(void)
{
	check(fdc_start(),"legacy drive ready");
	stream_begin(1); put32(SAVE_MAGIC); put32(2); put32(0); streamSum=0;
	put_state(); put_blocks(); put_chests();
	u32 sum=streamSum; stream_end_write();
	check(streamOk,"legacy write completes");
	disk[8]=sum; disk[9]=sum>>8; disk[10]=sum>>16; disk[11]=sum>>24;
	fdc_stop();
}
int main(void)
{
	setup(96,2,0xF0); memset(regionVisited,1,sizeof(regionVisited));
	int slot=mob_spawn(MOB_PIG,8*FU,FU,8*FU); u32 asleepID=g_mobIDs[slot];
	g_mobs[slot].health=6; mobs_regions_tick();
	slot=mob_spawn(MOB_SHEEP,48*FU,FU,48*FU); u32 awakeID=g_mobIDs[slot];
	g_mobs[slot].aiTimer=61;
	g_inv[0].item=B_PLANKS; g_inv[0].count=23;
	g_chests[0].used=1; g_chests[0].x=48; g_chests[0].y=1; g_chests[0].z=48;
	g_chests[0].slot[0].item=B_LOG; g_chests[0].slot[0].count=9;
	check(save_world()==SAVE_OK,"new save writes successfully");
	mobs_clear(); memset(blocks,B_AIR,sizeof(blocks)); chests_clear(); inv_clear();
	check(load_world()==SAVE_OK && save_loaded_mobs(),"new save loads region snapshots");
	check(dormantCount==1 && dormant[0].id==asleepID && dormant[0].mob.health==6,"dormant animal restored");
	int found=0;
	for(int i=0; i<MAX_MOBS; ++i) if(g_mobIDs[i]==awakeID && g_mobs[i].type==MOB_SHEEP && g_mobs[i].aiTimer==61) ++found;
	check(found==1 && regionVisited[0]==1,"active animal and initialized-empty region restored");
	check(blocks[widx(10,0,10)]==B_GRASS && g_inv[0].count==23 && g_chests[0].slot[0].count==9,"terrain, inventory and chest round trip");
	disk[8]^=1;
	check(load_world()==SAVE_BAD_DATA && !save_loaded_mobs(),"bad checksum is not a successful region load");
	disk[8]^=1;
	legacy_save(); mobs_clear();
	check(load_world()==SAVE_OK && !save_loaded_mobs(),"version 2 world loads without invented mob payload");
	printf("PASS: actual floppy save/load preserves active/dormant mobs, empty regions, terrain and chests; legacy and checksum checks\n");
	return 0;
}
