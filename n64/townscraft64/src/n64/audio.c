/* N64 audio interface in place of DOScraft's Sound Blaster stream.
 *
 * DOScraft's game_audio.c mixes 8-bit unsigned mono at DOS_SB_RATE (10989 Hz,
 * SB time constant 165) from its stream callback, which the SB IRQ called.
 * Here the audio interface runs at exactly twice that rate (21978 Hz, the
 * rate the music is rendered at, music.c); its buffer callback, also in
 * interrupt context, calls the same fill function and plays each effects
 * sample twice, widened to signed 16-bit, plus the music sample. */
#include <libdragon.h>
#include "sb_pcm.h"

#define MAX_FRAMES 2048
#define AI_RATE (2 * DOS_SB_RATE)

int n64_music_sample(void);

static void (*stream_fill)(unsigned char *, unsigned int);
static unsigned char mono[MAX_FRAMES];
static int audio_up;

static void ai_fill(short *buffer, size_t frames)
{
    /* frames at AI_RATE; the effects mixer runs at half that. */
    static int half;            /* 1: the last effects sample still owes its repeat */
    static int last;
    while (frames) {
        unsigned int want = (unsigned int)((frames + 1 - half) / 2);
        if (want > MAX_FRAMES) want = MAX_FRAMES;
        if (want) {
            if (stream_fill)
                stream_fill(mono, want);
            else
                memset(mono, 128, want);
        }
        unsigned int k = 0;
        while (frames && (half || k < want)) {
            int fx;
            if (half) { fx = last; half = 0; }
            else { fx = last = ((int)mono[k++] - 128) << 8; half = 1; }
            int v = fx + n64_music_sample();
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            buffer[0] = buffer[1] = (short)v;
            buffer += 2;
            frames--;
        }
    }
}

int dos_sb_init(void)
{
    if (!audio_up) {
        audio_init(AI_RATE, AUDIO_INIT_LATENCY_MS(60));   /* ~ the SB stream blocks */
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

