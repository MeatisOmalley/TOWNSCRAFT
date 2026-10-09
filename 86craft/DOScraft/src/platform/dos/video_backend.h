#ifndef DOSCRAFT_VIDEO_BACKEND_H
#define DOSCRAFT_VIDEO_BACKEND_H
/* Bind the game's IRQ clock after PIT reprogramming. NULL restores the
 * standalone diagnostics' default BIOS/PIT uclock timebase. */
void dos_video_set_tick_clock(volatile unsigned int *ticks, unsigned int rate);

void video_init(void);
void video_set_palette(int index, int red, int green, int blue);
int video_in_vsync(void);
void video_wait_vsync(void);
void dos_video_restore(void);
void dos_video_present(const unsigned char *source, unsigned int pitch);
/* Diagnostic only, deliberately reads VRAM; not a rendering hot path. */
int dos_video_verify(const unsigned char *source, unsigned int pitch);

#endif
