#ifndef DOS_AUDIO_BANK_H
#define DOS_AUDIO_BANK_H
enum { A_CRUNCH,A_STONE,A_WOOD,A_STEP_GRASS,A_STEP_STONE,A_STEP_WOOD,
       A_PLACE,A_HURT,A_GLASS,A_CLICK,A_SAND,A_GROUPS };
typedef struct { const unsigned char *data; unsigned int frames,rate; } DosAudioClip;
/* Startup only. External unsigned 8-bit mono PCM, up to 192 KiB; no codec in DOS. */
int dos_audio_bank_load(const char *path);
int dos_audio_bank_parse(const unsigned char *data,unsigned int bytes);
unsigned int dos_audio_bank_count(int group);
const DosAudioClip *dos_audio_bank_clip(int group,unsigned int variant);
#endif
