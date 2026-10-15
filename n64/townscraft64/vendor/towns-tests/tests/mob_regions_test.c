/* Real mob manager and codec, with the spawn regression's terrain fixture. */
#define main spawn_regressions_main
#include "mobs_test.c"
#undef main
static u32 encoded[8192];
static int written,readPos;
static void put_word(u32 v) { check(written<ARRAY_LEN(encoded),"codec buffer"); encoded[written++]=v; }
static u32 get_word(void) { return readPos<written ? encoded[readPos++] : 0; }
static int by_id(u32 id)
{
	for(int i=0; i<MAX_MOBS; ++i) if(g_mobs[i].type && g_mobIDs[i]==id) return i;
	return -1;
}
static void isolate(void)
{
	setup(96,2,0xF0);
	memset(regionVisited,1,sizeof(regionVisited));
}
int main(void)
{
	Mob expected;
	u32 id;
	int slot;
	isolate();
	slot=mob_spawn(MOB_PIG,8*FU,2*FU,8*FU); id=g_mobIDs[slot];
	g_mobs[slot].body.vx=123; g_mobs[slot].body.vy=-88;
	g_mobs[slot].body.fallDist=777; g_mobs[slot].panic=17;
	g_mobs[slot].health=7; g_mobs[slot].aiTimer=53;
	g_mobs[slot].body.onGround=1; g_mobs[slot].hurtTimer=6;
	expected=g_mobs[slot];
	mobs_regions_tick();
	check(!g_mobs[slot].type && dormantCount==1,"animal releases active slot");
	for(int t=0; t<100; ++t) mobs_tick(0);
	check(!memcmp(&dormant[0].mob,&expected,sizeof(Mob)),"dormant animals receive no ticks");
	g_player.body.x=g_player.body.z=8*FU;
	mobs_regions_tick(); slot=by_id(id);
	check(slot>=0 && !dormantCount && !memcmp(&g_mobs[slot],&expected,sizeof(Mob)),"exact state and identity restored");
	for(int n=0; n<20; ++n)
	{
		g_player.body.x=g_player.body.z=48*FU; mobs_regions_tick();
		g_player.body.x=g_player.body.z=8*FU; mobs_regions_tick();
		check(mobs_count(0)==1 && !dormantCount && by_id(id)>=0,"crossings never duplicate animals");
	}
	/* Slot exhaustion defers restoration without changing its saved state. */
	g_player.body.x=g_player.body.z=48*FU; mobs_regions_tick();
	g_player.body.x=g_player.body.z=8*FU;
	for(int n=0; n<MAX_MOBS; ++n) mob_spawn(MOB_ZOMBIE,8*FU,FU,8*FU);
	mobs_regions_tick();
	check(dormantCount==1 && by_id(id)<0,"full active pool preserves dormant owner");
	g_mobs[3].type=MOB_NONE; mobs_regions_tick();
	check(!dormantCount && by_id(id)==3,"activation succeeds when a slot opens");
	g_mobs[3].hurtTimer=0; mob_hurt(3,100,0,0,0);
	g_player.body.x=g_player.body.z=48*FU; mobs_regions_tick();
	g_player.body.x=g_player.body.z=8*FU; mobs_regions_tick();
	check(by_id(id)<0 && !dormantCount,"killed animal stays dead in an initialized region");
	/* Hostiles unload, but active explosives keep simulating. */
	isolate();
	int hostile=mob_spawn(MOB_ZOMBIE,8*FU,FU,8*FU);
	int tnt=mob_spawn(MOB_TNT,8*FU,FU,8*FU);
	int creeper=mob_spawn(MOB_CREEPER,8*FU,FU,8*FU); g_mobs[creeper].fuse=5;
	mobs_regions_tick();
	check(!g_mobs[hostile].type && g_mobs[tnt].type && g_mobs[creeper].type && !dormantCount,"explosives are not dormant animals");
	/* Loading a far fuse cannot tick or explode against missing terrain. */
	isolate(); tnt=mob_spawn(MOB_TNT,8*FU,FU,8*FU); g_mobs[tnt].fuse=59;
	short missing[256]; memset(missing,0xFF,sizeof(missing)); g_columnMap=missing;
	mobs_tick(0);
	check(g_mobs[tnt].fuse==59 && g_mobs[tnt].type==MOB_TNT,"unprepared blast terrain pauses loaded fuse");
	int cx,cz;
	check(mobs_column_needed(&cx,&cz) && cx==0 && cz==0,"missing fuse footprint requests priority terrain");
	g_columnMap=NULL; mobs_tick(0);
	check(g_mobs[tnt].fuse==58,"loaded fuse resumes after collision and blast terrain ready");
	/* Both active and dormant records round-trip through explicit fields. */
	isolate();
	slot=mob_spawn(MOB_SHEEP,8*FU,2*FU,8*FU); id=g_mobIDs[slot];
	g_mobs[slot].health=5; expected=g_mobs[slot]; mobs_regions_tick();
	int near=mob_spawn(MOB_PIG,48*FU,FU,48*FU); u32 nearID=g_mobIDs[near];
	written=0; mobs_save(put_word); mobs_clear(); readPos=0;
	check(mobs_load(get_word) && readPos==written,"mob codec consumes complete record");
	check(dormantCount==1 && dormant[0].id==id && !memcmp(&dormant[0].mob,&expected,sizeof(Mob)),"dormant snapshot persists");
	check(by_id(nearID)>=0 && regionVisited[0]==1,"active snapshot and empty initialized regions persist");
	int first=3+g_NC*g_NC+2;
	encoded[first+21]=encoded[first]; readPos=0;
	check(!mobs_load(get_word),"duplicate identities rejected");
	/* Full dormant store retains an active owner instead of discarding it. */
	isolate(); dormantCount=MAX_DORMANT_MOBS;
	slot=mob_spawn(MOB_PIG,8*FU,FU,8*FU); id=g_mobIDs[slot];
	for(int n=0; n<dormantCount; ++n) { dormant[n].mob=g_mobs[slot]; dormant[n].id=1000+n; }
	mobs_regions_tick();
	check(by_id(id)>=0 && dormantCount==MAX_DORMANT_MOBS,"full dormant store never loses an animal");
	printf("PASS: mob ownership, exact freezing, repeated regions, slot pressure, deaths, fuses and persistence\n");
	return 0;
}
