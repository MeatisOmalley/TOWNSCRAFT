/* N64 audio interface in place of DOScraft's Sound Blaster stream.
 *
 * DOScraft's game_audio.c mixes 8-bit unsigned mono at DOS_SB_RATE (10989 Hz,
 * SB time constant 165) from its stream callback, which the SB IRQ called.
 * Here the N64 audio interface runs at that same rate (no resampling) and
 * its buffer callback, also in interrupt context, calls the same fill
 * function; each sample is widened to signed 16-bit on both channels. */
#include <libdragon.h>
#include "sb_pcm.h"

#define MAX_FRAMES 2048

static void (*stream_fill)(unsigned char *, unsigned int);
static unsigned char mono[MAX_FRAMES];
static int audio_up;

static void ai_fill(short *buffer, size_t frames)
{
    while (frames) {
        unsigned int n = frames > MAX_FRAMES ? MAX_FRAMES : frames;
        if (stream_fill)
            stream_fill(mono, n);
        else
            memset(mono, 128, n);
        for (unsigned int i = 0; i < n; i++) {
            short v = (short)(((int)mono[i] - 128) << 8);
            buffer[2 * i] = buffer[2 * i + 1] = v;
        }
        buffer += 2 * n;
        frames -= n;
    }
}

int dos_sb_init(void)
{
    if (!audio_up) {
        audio_init(DOS_SB_RATE, AUDIO_INIT_LATENCY_MS(60));   /* ~ the SB stream blocks */
        audio_up = 1;
    }
    return 0;
}

int dos_sb_stream_start(void (*fill)(unsigned char *, unsigned int))
{
    stream_fill = fill;
    audio_set_buffer_callback(ai_fill);
    /* The callback runs from the AI interrupt, which only fires when a queued
       buffer ends: queue the first one to start the stream. */
    audio_write_silence();
    return 0;
}

void dos_sb_shutdown(void)
{
    audio_set_buffer_callback(NULL);
    stream_fill = NULL;
}

/* No PC speaker on the N64. */
void dos_video_set_speaker_clock(int enabled) { (void)enabled; }

/* Music: DOScraft drives an OPL2 FM chip, which the N64 lacks. Silent until
 * the N64 music player lands. */
unsigned int dos_music_notes;
int dos_music_init(void) { return -1; }
void dos_music_tick(void) {}
void dos_music_shutdown(void) {}
const char *dos_music_backend(void) { return "none"; }
unsigned int dos_music_songs(void) { return 0; }
int dos_music_current(void) { return -1; }
int dos_music_playing(void) { return 0; }
const char *dos_music_song_name(int piece) { (void)piece; return ""; }
void music_schedule(int ticks) { (void)ticks; }
void music_set_gap(int ticks) { (void)ticks; }
void music_stop(void) {}
