/* Native 32-bit tests using the game's real movement and collision code.
   gcc -m32 -O2 -ffreestanding -fno-builtin -Isrc tests/player_test.c
       src/player.c src/physics.c src/blocks.c src/fmath.c src/libc.c
       build/tables.c -o build/player_test
*/
#include "player.h"
#include "world.h"

extern int printf(const char *,...);
extern void exit(int);

int g_W=16,g_NC=1;
int g_spawnX=4,g_spawnY=1,g_spawnZ=4;
static u8 blocks[16*16*WH];
static int offsets[16];
u8 *g_blocks=blocks;
int *g_zOff=offsets;

void sound_play(int sfx,int pitch,int vol,int pan)
{
	(void)sfx; (void)pitch; (void)vol; (void)pan;
}
int world_is_solid(int x,int y,int z)
{
	return 0!=(blk_flags(wget(x,y,z))&BF_SOLID);
}
static void check(int condition,const char *message)
{
	if(!condition) { printf("FAIL: %s\n",message); exit(1); }
}
static void setup(void)
{
	memset(blocks,0,sizeof(blocks));
	for(int z=0;z<16;++z)
	{
		offsets[z]=z*16*WH;
		for(int x=0;x<16;++x) blocks[widx(x,0,z)]=B_STONE;
	}
	player_init();
	PlayerInput in={0};
	player_tick(&in);
	check(g_player.body.onGround,"starts grounded");
}
int main(void)
{
	PlayerInput in={0};
	setup();
	in.jump=1;
	int takeoffs=0,peak=FU;
	for(int i=0;i<60;++i)
	{
		int grounded=g_player.body.onGround;
		player_tick(&in);
		if(grounded && !g_player.body.onGround) ++takeoffs;
		peak=MAX(peak,g_player.body.y);
	}
	check(takeoffs==1,"holding jump produces one takeoff");
	check(peak>2*FU && peak<5*FU/2,"jump clears one block, not 1.5 blocks");
	check(g_player.body.onGround,"lands while jump is held");
	in.jump=0; player_tick(&in);
	in.jump=1; player_tick(&in);
	check(g_player.body.vy>0 && !g_player.body.onGround,"release then press jumps again");
	in.jump=0; player_tick(&in);
	in.jump=1;
	for(int i=0;i<60;++i) player_tick(&in);
	check(g_player.body.onGround,"airborne press does not auto-jump on landing");

	setup();
	for(int z=6;z<16;++z)
	for(int x=0;x<16;++x) blocks[widx(x,1,z)]=B_STONE;
	g_player.body.z=5*FU+FU/2;
	in.forward=1;
	for(int i=0;i<20;++i) player_tick(&in);
	check(g_player.body.onGround && g_player.body.y==2*FU && g_player.body.z>6*FU,
	      "jumps onto and lands on a one-block ledge");

	setup(); in.forward=0;
	for(int z=0;z<16;++z)
	for(int x=0;x<16;++x) blocks[widx(x,3,z)]=B_STONE;
	for(int i=0;i<30;++i)
	{
		player_tick(&in);
		check(g_player.body.y+g_player.body.h<=3*FU,"ceiling collision prevents penetration");
	}
	check(g_player.body.onGround,"lands after hitting ceiling");

	setup();
	for(int z=0;z<16;++z)
	for(int x=0;x<16;++x)
	for(int y=1;y<7;++y) blocks[widx(x,y,z)]=B_WATER;
	in.jump=0; player_tick(&in);
	in.jump=1;
	for(int i=0;i<15;++i) player_tick(&in);
	check(g_player.body.y>2*FU,"holding jump still swims upward");
	printf("PASS: jump peak %.3f blocks; single press, ledge, ceiling and swimming checks\n",
	       (peak-FU)/(double)FU);
	return 0;
}
