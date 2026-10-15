/* Sound effects (RF5c68 PCM) and music (YM2612 FM). */
#ifndef SOUND_H
#define SOUND_H
#include "common.h"

enum
{
	SFX_CRUNCH,    /* dirt, grass, sand, leaves, eating */
	SFX_STONE,
	SFX_WOOD,      /* wood, doors */
	SFX_STEP,
	SFX_PLACE,
	SFX_HURT,
	SFX_EXPLODE,
	SFX_HISS,      /* creeper fuse */
	SFX_CLICK,
	SFX_RAIN,      /* Loop */
	NUM_SFX
};

void sound_init(void);

/* pitch: 256 = the sample's own pitch.  vol: 0..255.  pan: -15 (left) ..
   15 (right). */
void sound_play(int sfx,int pitch,int vol,int pan);

/* Positional: the listener is set every frame (fine units, yaw 0..1023),
   the source is in fine units too.  Volume falls off over ~16 blocks. */
void sound_set_listener(int x,int y,int z,int yaw);
void sound_play_at(int sfx,int pitch,int vol,int x,int y,int z);

/* A looping sound (rain) on its own channel; vol 0 stops it */
void sound_loop(int sfx,int vol);

/* Music plays now and then: music_schedule(ticks) sets the silence before
   the next play-through (100 ticks per second); after a play-through the
   silence is minGap plus up to minGap more. */
void music_schedule(int ticks);
void music_set_gap(int minGap);
void music_stop(void);
extern int g_musicOn,g_sfxOn;

/* Called by the timer interrupt (100 Hz) */
void sound_tick(void);

#endif
