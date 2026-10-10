/* Portable assets for the direct SB1/OPL2 port; not a playback driver. */
#ifndef DOSCRAFT_SOUND_ASSETS_H
#define DOSCRAFT_SOUND_ASSETS_H
typedef struct {
    unsigned int offset,frames;
    unsigned short pitch;
    unsigned char volume;
} DosSoundSample;
typedef struct {
    unsigned short tick;
    unsigned char note,voice;
} DosSoundEvent;

/* Startup-only synthesis. Rebuilding restores the fresh-process source seed
 * and score; callers must stop playback before rebuilding borrowed storage.
 * Returns 0, or -1 for any capacity/descriptor/score validation failure. */
int dos_sound_assets_build(void);
/* Borrowed unsigned 8-bit waveform. Original sample alignment/offsets remain;
 * RF5c68 end markers and padding become unsigned silence. Length excludes each
 * effect's marker. Preserve the base pitch separately; no baked resampling. */
const unsigned char *dos_sound_wave(unsigned int *bytes);
const DosSoundSample *dos_sound_sample(int id);
const DosSoundEvent *dos_sound_events(unsigned int *count,unsigned int *song_ticks);
#endif
