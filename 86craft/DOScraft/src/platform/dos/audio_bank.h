#ifndef DOS_AUDIO_BANK_H
#define DOS_AUDIO_BANK_H
enum { A_CRUNCH,A_STONE,A_WOOD,A_STEP_GRASS,A_STEP_STONE,A_STEP_WOOD,
       A_PLACE,A_HURT,A_GLASS,A_CLICK,A_SAND,
       A_EXPLODE,A_HISS,A_GROAN,A_GROUPS };
typedef struct { const unsigned char *data; unsigned int frames,rate; } DosAudioClip;
/* Startup only, after the game arenas. External unsigned 8-bit mono PCM, up to
 * 192 KiB; no codec in DOS. Memory is allocated for the bank's actual size;
 * when that fails, one variant per group loads ("LITE"). */
int dos_audio_bank_load(const char *path);
int dos_audio_bank_parse(const unsigned char *data,unsigned int bytes);
const char *dos_audio_bank_mode(void);
unsigned int dos_audio_bank_bytes(void);
/* Test hook: a nonzero limit makes larger allocations fail. */
extern unsigned int dos_audio_bank_memory_limit;
unsigned int dos_audio_bank_count(int group);
const DosAudioClip *dos_audio_bank_clip(int group,unsigned int variant);
#endif
