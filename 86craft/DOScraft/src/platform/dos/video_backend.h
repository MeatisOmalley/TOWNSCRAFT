#ifndef DOSCRAFT_VIDEO_BACKEND_H
#define DOSCRAFT_VIDEO_BACKEND_H
/* Bind the game's IRQ clock after PIT reprogramming. NULL restores the
 * standalone diagnostics' default BIOS/PIT uclock timebase. */
void dos_video_set_tick_clock(volatile unsigned int *ticks, unsigned int rate);
/* Performance hint only. Broadcast always proves exact row equality; disable
 * planning for full-resolution views where adjacent pixels usually differ. */
void dos_video_set_repeated_pixel_hint(int enabled);
#ifdef DOS_VGA_PROVEN_ROWS
/* Exact row proof for the next scale-2 view. End is exclusive.
 * A presentation consumes the proof; no later page can inherit it. */
void dos_video_prove_low_rows(unsigned int begin, unsigned int end);
void dos_video_unprove_low_rows(unsigned int begin, unsigned int end);
#endif

void video_init(void);
void video_set_palette(int index, int red, int green, int blue);
int video_in_vsync(void);
void video_wait_vsync(void);
void dos_video_restore(void);
void dos_video_present(const unsigned char *source, unsigned int pitch);
/* Diagnostic only, deliberately reads VRAM; not a rendering hot path. */
int dos_video_verify(const unsigned char *source, unsigned int pitch);

#endif
