/* Native 32-bit spawn regressions using real mobs, blocks and lighting rules. */
#include "../src/mobs.c"

extern int printf(const char *,...);
extern void exit(int);
extern void *calloc(unsigned int,unsigned int);
void *heap_alloc_low(u32 n) { return calloc(1,n); }
int g_W=96,g_NC=6;
u32 g_ramMB=2;
Player g_player;
static u8 blocks[256*256*WH],lights[256*256*WH];
static int offsets[256];
u8 *g_blocks=blocks,*g_light=lights;
int *g_zOff=offsets;
short *g_columnMap,*g_columnCoords;
u8 *g_columnReady;
int g_residentColumns,g_allocChunkCount;
void sound_play_at(int sfx,int pitch,int vol,int x,int y,int z) {}
void world_set(int x,int y,int z,u8 b) { blocks[widx(x,y,z)]=b; }
int chest_remove(int x,int y,int z,int giveItems) { return 0; }
void player_hurt(int dmg,int fromX,int fromZ) {}
void body_tick(Body *b,int gravity) {}
int face_light_level(int light,int dir,int darken) { return MAX((light>>4)-darken,light&15); }
MBox *render_add_box(void) { return NULL; }
int inv_add(int item,int count) { return count; }
void game_message(const char *message) {}
int world_surface_y(int x,int z)
{
	for(int y=WH-1; y>0; --y) if(blk_flags(wget(x,y-1,z))&BF_SOLID) return y;
	return 1;
}
int world_light_at(int x,int y,int z) { return g_light[widx(x,y,z)]; }
static void check(int condition,const char *message)
{
	if(!condition) { printf("FAIL W=%d RAM=%u: %s\n",g_W,g_ramMB,message); exit(1); }
}
static void setup(int width,int ram,int light)
{
	g_W=width; g_NC=width/CS; g_ramMB=ram;
	memset(blocks,B_AIR,sizeof(blocks));
	memset(lights,light,sizeof(lights));
	for(int z=0; z<g_W; ++z)
	{
		offsets[z]=z*g_W*WH;
		for(int x=0; x<g_W; ++x) blocks[widx(x,0,z)]=B_GRASS;
	}
	g_player.body.x=g_player.body.z=g_W/2*FU;
	g_player.body.y=FU;
	mobs_clear(); rnd_seed(4242);
}
int main(void)
{
	int widths[]={96,160,208,256};
	for(int w=0; w<ARRAY_LEN(widths); ++w)
	{
		setup(widths[w],w==0 ? 2 : 4,0xF0);
		mobs_spawn_initial(4242);
		check(mobs_count(0)>0 && mobs_count(0)<=passive_limit(),"nearby regions honor reserved slots");
		while(mobs_count(0)<passive_limit()) check(mob_spawn(MOB_PIG,g_W/2*FU,FU,g_W/2*FU)>=0,"fill animal budget");
		for(int i=0; i<100; ++i) mobs_try_spawn(0);
		check(!mobs_count(1),"daylight does not spawn hostiles");
		for(int i=0; i<100; ++i) mobs_try_spawn(11);
		check(mobs_count(1)==hostile_limit(),"night spawning fills hostile cap alongside animals");
		check(mob_spawn(MOB_TNT,8*FU,FU,8*FU)>=0,"TNT retains a free slot");
		setup(widths[w],w==0 ? 2 : 4,0xFF);
		for(int i=0; i<100; ++i) mobs_try_spawn(11);
		check(!mobs_count(1),"torch light suppresses night spawning");
	}
	setup(96,2,0xF0);
	for(int z=0; z<g_W; ++z)
	for(int x=0; x<g_W; ++x)
	{
		blocks[widx(x,4,z)]=B_STONE;
		blocks[widx(x,8,z)]=B_STONE;
		lights[widx(x,5,z)]=0;
	}
	g_player.body.y=5*FU;
	for(int i=0; i<100; ++i) mobs_try_spawn(0);
	check(mobs_count(1)==hostile_limit(),"dark cave floors spawn hostiles during daytime");
	for(int i=0; i<MAX_MOBS; ++i)
		if(is_hostile(g_mobs[i].type)) check(g_mobs[i].body.y==5*FU,"hostiles use the cave floor");
	printf("PASS: spawn reserves, night/day, torches and cave floors\n");
	return 0;
}
