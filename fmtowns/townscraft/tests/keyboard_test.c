/* Native 32-bit regression for the actual keyboard packet decoder. */
#include "../src/sys.c"
#include "player.h"
#include "world.h"

extern int printf(const char *,...);
extern void exit(int);
int g_W=64,g_NC=4,g_spawnX=8,g_spawnY=1,g_spawnZ=8;
static u8 blocks[64*64*WH];
static int offsets[64];
u8 *g_blocks=blocks;
int *g_zOff=offsets;
short *g_columnMap;
void sound_play(int sfx,int pitch,int vol,int pan) {}
/* Hardware/rendering entry points are linked but never run by this test. */
void sound_tick(void) {}
u8 __bss_end[1],*g_fb;
void gfx_clear(u8 *fb,u8 color) {}
void gfx_text(u8 *fb,int x,int y,const char *text,u8 color) {}
void gfx_present(void) {}
#define EXC(n) void exc##n(void) {}
EXC(0) EXC(1) EXC(2) EXC(3) EXC(4) EXC(5) EXC(6) EXC(7) EXC(8) EXC(9)
EXC(10) EXC(11) EXC(12) EXC(13) EXC(14) EXC(15) EXC(16) EXC(17) EXC(18) EXC(19)
void irq_timer(void) {}
void irq_vsync(void) {}
void irq_master_spurious(void) {}
void irq_slave_spurious(void) {}
int world_is_solid(int x,int y,int z) { return (blk_flags(wget(x,y,z))&BF_SOLID)!=0; }
static void check(int condition,const char *message)
{
	if(!condition) { printf("FAIL: %s\n",message); exit(1); }
}
static void packet(u8 flags,u8 key) { kbd_byte(flags); kbd_byte(key); }
int main(void)
{
	for(int z=0; z<g_W; ++z)
	{
		offsets[z]=z*g_W*WH;
		for(int x=0; x<g_W; ++x) blocks[widx(x,0,z)]=B_STONE;
	}
	player_init();
	PlayerInput in={0};
	player_tick(&in);
	int start=g_player.body.z;
	packet(0xC0,KEY_W);
	for(int tick=0; tick<60; ++tick)
	{
		if(tick>=20) packet(0xF0,KEY_W);
		check(g_keyDown[KEY_W],"W remains held through typematic repeat");
		in.forward=g_keyDown[KEY_W];
		int before=g_player.body.z;
		player_tick(&in);
		check(g_player.body.z>before,"walking continues for the full three seconds");
	}
	check(g_player.body.z-start>6*FU,"held walk covers more than one second of movement");
	packet(0xC0,KEY_D);
	packet(0xF3,KEY_W); /* Repeat with CTRL/SHIFT flags */
	check(g_keyDown[KEY_W] && g_keyDown[KEY_D],"simultaneous keys survive repeats");
	packet(0xD0,KEY_W);
	check(!g_keyDown[KEY_W] && g_keyDown[KEY_D],"real release affects only that key");
	packet(0xD3,KEY_D);
	check(!g_keyDown[KEY_D],"release with modifiers clears held key");
	packet(0xC0,KEY_W);
	check(g_keyDown[KEY_W],"can press again after releasing");
	printf("PASS: sustained walking, repeats, simultaneous keys and real releases\n");
	return 0;
}
