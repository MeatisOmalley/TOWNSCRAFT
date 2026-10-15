/* Real cache + mobs + DMA driver: paging, bank commits and failure recovery. */
#define COLUMN_CACHE_FIXTURE
#include "column_cache_test.c"
#include "../src/mobs.c"
#include "../src/save.h"
#include "../src/game.h"
Player g_player;
Slot g_inv[INV_SLOTS];
Chest g_chests[MAX_CHESTS];
int g_time=1000;
void chests_clear(void) { memset(g_chests,0,sizeof(g_chests)); }
void inv_clear(void) { memset(g_inv,0,sizeof(g_inv)); }
void sound_play_at(int sfx,int pitch,int vol,int x,int y,int z) {}
int chest_remove(int x,int y,int z,int giveItems) { return 0; }
void player_hurt(int dmg,int fromX,int fromZ) {}
void body_tick(Body *b,int gravity) {}
int face_light_level(int light,int dir,int darken) { return MAX((light>>4)-darken,light&15); }
MBox *render_add_box(void) { return NULL; }
int inv_add(int item,int count) { return count; }
void game_message(const char *message) {}
static int failWrite=-1,writeCount,readCount;
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
		else
        {
            if(writeCount++==failWrite) { diskStatus=0x40; return; }
            memcpy(disk+track*TRACK_BYTES,trackBuf,TRACK_BYTES);
        }
        if(v==0x90) ++readCount;
        diskStatus=0;
	}
}

static void travel(int x,int z)
{
    for(int tick=0; tick<2000; ++tick)
    {
        world_stream(x,z,20,1); save_stream_tick();
        int ready=1;
        for(int cz=MAX(0,z/CS-2); cz<=MIN(g_NC-1,z/CS+2); ++cz)
        for(int cx=MAX(0,x/CS-2); cx<=MIN(g_NC-1,x/CS+2); ++cx)
            if(!world_column_ready(cx,cz)) ready=0;
        if(ready && cacheLightSlot<0) return;
    }
    check(0,"cooperative travel eventually prepares its neighborhood");
}
#ifndef COLUMN_SAVE_FIXTURE
int main(void)
{
    g_ramMB=2; reference_world(96,4242); world_alloc(); world_generate(4242);
    g_player.body.x=g_player.body.z=8*FU; g_player.body.y=40*FU;
    world_stream(8,8,20,100000); mobs_clear();
    memset(regionVisited,1,sizeof(regionVisited));
    int slot=mob_spawn(MOB_PIG,80*FU,FU,80*FU); u32 asleepID=g_mobIDs[slot];
    g_mobs[slot].health=6; mobs_regions_tick();
    slot=mob_spawn(MOB_SHEEP,8*FU,FU,8*FU); u32 awakeID=g_mobIDs[slot];
    g_mobs[slot].aiTimer=61;
    world_set(8,40,8,B_PLANKS); world_set(9,40,8,B_TORCH);
    g_inv[0].item=B_LOG; g_inv[0].count=9;
    check(save_world()==SAVE_OK,"first bank commits");
    check(columnTop==0 && columnRecords[0].disk,"save releases all compressed baseline backing to disk");
    /* Four column-sized requests touching two physical tracks need two
       reads, including a record spanning the cached first track. */
    page_cancel();
    int cachedReads=readCount,status;
    do { status=save_column_read(512,1000,columnScratch); } while(!status);
    check(status==1 && !memcmp(columnScratch,disk+512,1000),"first buffered record matches disk");
    do { status=save_column_read(1800,900,columnScratch); } while(!status);
    check(status==1 && readCount==cachedReads+1 && !memcmp(columnScratch,disk+1800,900),"same-track record avoids another disk read");
    pageIdle=0; clockTicks+=201; save_stream_tick();
    check(!pageMotor,"cached-read fixture stops idle motor");
    do { status=save_column_read(TRACK_BYTES-200,400,columnScratch); } while(!status);
    check(status==1 && readCount==cachedReads+2 && !memcmp(columnScratch,disk+TRACK_BYTES-200,400),"spanning record reuses cached prefix after motor stop");
    do { status=save_column_read(TRACK_BYTES+500,700,columnScratch); } while(!status);
    check(status==1 && readCount==cachedReads+2 && !memcmp(columnScratch,disk+TRACK_BYTES+500,700),"next record reuses second buffered track");
    printf("PASS: four paging requests across two tracks use two physical reads\n");
    page_cancel(); preparedColumn=-1;
    int reads=readCount;
    travel(88,88); travel(8,8);
    check(readCount>reads && pageState==5,"travel actually uses cooperative disk reads");
    check(wget(8,40,8)==B_PLANKS && world_light_at(10,40,8)%16==13,"disk paging restores edits and torch");
    world_set(8,40,8,B_STONE);
    failWrite=writeCount+2;
    check(save_world()==SAVE_DISK_ERROR,"interrupted second bank fails");
    failWrite=-1;
    check(load_world()==SAVE_OK && save_loaded_mobs(),"previous bank loads after interrupted save");
    world_rebuild_after_load();
    check(wget(8,40,8)==B_PLANKS,"previous committed edits remain intact");
    check(dormantCount==1 && dormant[0].id==asleepID && dormant[0].mob.health==6,"dormant animal survives transaction");
    int found=0;
    for(int i=0; i<MAX_MOBS; ++i) if(g_mobIDs[i]==awakeID && g_mobs[i].type==MOB_SHEEP && g_mobs[i].aiTimer==61) ++found;
    check(found==1 && g_inv[0].count==9,"active animal and inventory survive transaction");
    world_set(8,40,8,B_STONE);
    u32 capacity=columnCapacity; columnCapacity=0;
    check(save_world()==SAVE_OK,"second bank commits successfully");
    columnCapacity=capacity;
    check(load_world()==SAVE_OK,"newest bank loads"); world_rebuild_after_load();
    check(wget(8,40,8)==B_STONE,"latest committed terrain selected");
    disk[BANK_TRACKS*TRACK_BYTES+25]^=1;
    check(load_world()==SAVE_OK,"corrupt newest bank falls back to older bank"); world_rebuild_after_load();
    check(wget(8,40,8)==B_PLANKS,"fallback restores exact older terrain");
    travel(88,88); travel(8,8);
    check(wget(8,40,8)==B_PLANKS,"fallback also pages from the correct bank");
    travel(48,48); travel(8,8);
    check(g_columnMap[35]<0,"corruption test targets a truly evicted column");
    int owner=g_columnMap[0]; ColumnRecord *far=&columnRecords[35];
    u8 saved=disk[far->off]; disk[far->off]^=1;
    for(int tick=0; tick<1000 && !world_stream_error(); ++tick) cache_stream(88,88,1);
    check(world_stream_error()==1 && g_columnMap[0]==owner && wget(8,40,8)==B_PLANKS,"runtime disk corruption retains resident owners");
    disk[far->off]=saved; preparedColumn=-1; page_cancel();
    travel(88,88); travel(8,8);
    /* Import the original continuous RLE layout, without regenerating it. */
    page_cancel(); check(fdc_start(),"legacy import drive ready");
    streamBase=0; streamLimit=NUM_TRACKS; stream_begin(1);
    put32(SAVE_MAGIC); put32(2); put32(0); streamSum=0; put_state();
    u8 *resident=g_blocks; g_blocks=referenceBlocks; put_blocks(); g_blocks=resident;
    put_chests(); u32 checksum=streamSum; stream_end_write();
    check(streamOk,"legacy reference writes");
    disk[8]=checksum; disk[9]=checksum>>8; disk[10]=checksum>>16; disk[11]=checksum>>24;
    fdc_stop();
    check(load_world()==SAVE_OK && !save_loaded_mobs(),"legacy world imports into resident cache");
    check(genVersion==0,"import does not claim to know a legacy world's seed");
    world_rebuild_after_load(); compare_resident(8,8);
    /* Keep the original 8 MB profile's larger full-disk saves working. */
    g_ramMB=8; reference_world(256,4242); world_alloc(); world_generate(4242); mobs_clear();
    g_player.body.x=g_spawnX*FU; g_player.body.z=g_spawnZ*FU;
    check(save_world()==SAVE_OK,"256-wide world retains full-disk save compatibility");
    check(load_world()==SAVE_OK,"256-wide legacy-format cache reloads");
    world_rebuild_after_load(); compare_resident(g_spawnX,g_spawnZ);
    printf("PASS: actual floppy paging, exact terrain/light/mobs, bounded baseline RAM, interrupted writes and corrupt-bank recovery\n");
    return 0;
}
#endif
