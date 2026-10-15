#include <stdio.h>
#include "player.h"
#include "world.h"
#include "fmath.h"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

/* The real player.c over a world with no terrain and no physics. */
int g_spawnX,g_spawnY,g_spawnZ;
int world_is_solid(int x,int y,int z) { (void)x;(void)y;(void)z; return 0; }
void body_tick(Body *b,int gravity) { (void)gravity; b->onGround=1; }
/* Yaw 0 only: sin 0, cos 1 (the table is read at index 0 and 256). */
const short g_sinTab[1024]={[256]=16384};
u32 isqrt(u32 v) { u32 r=0; while((r+1)*(r+1)<=v) ++r; return r; }
void sound_play(int sfx,int pitch,int vol,int pan) { (void)sfx;(void)pitch;(void)vol;(void)pan; }
void game_message(const char *msg) { (void)msg; }

static void ticks(const PlayerInput *in,int n) { while(n-->0) player_tick(in); }

int main(void)
{
    PlayerInput still={0,0,0,0},sprint={1,0,0,1};
    player_init();
    CHECK(g_player.hunger==MAX_HUNGER && g_player.health==MAX_HEALTH);
    /* Standing still: one point a minute, a full bar for a 20-minute day. */
    ticks(&still,EX_POINT);
    CHECK(g_player.hunger==MAX_HUNGER-1);
    /* Sprinting triples the drain. */
    ticks(&sprint,EX_POINT/(1+EX_SPRINT));
    CHECK(g_player.hunger==MAX_HUNGER-2);
    /* Healing needs a nearly full bar and costs hunger. */
    g_player.health=10; g_player.hunger=18; g_player.exhaustion=0; g_player.regenTimer=0;
    ticks(&still,80);
    CHECK(g_player.health==11 && g_player.exhaustion==80+EX_HEAL);
    g_player.hunger=17; g_player.regenTimer=0;
    ticks(&still,80);
    CHECK(g_player.health==11);
    /* Too hungry to sprint: the walk speed stays the same. */
    g_player.hunger=6; g_player.body.vz=0;
    ticks(&sprint,10);
    {
        int slow=g_player.body.vz;
        g_player.hunger=7; g_player.body.vz=0;
        ticks(&sprint,10);
        CHECK(g_player.body.vz>slow);
    }
    /* Starving takes health down to one heart, never lower. */
    g_player.hunger=0; g_player.health=6; g_player.exhaustion=0;
    ticks(&still,80*10);
    CHECK(g_player.health==2 && !g_player.dead);
    /* Respawning refills the bar. */
    player_respawn();
    CHECK(g_player.hunger==MAX_HUNGER && !g_player.exhaustion);
    printf("PASS: hunger\n");
    return 0;
}
