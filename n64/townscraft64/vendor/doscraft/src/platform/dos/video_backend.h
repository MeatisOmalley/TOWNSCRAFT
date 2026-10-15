#ifndef DOSCRAFT_VIDEO_BACKEND_H
#define DOSCRAFT_VIDEO_BACKEND_H
/* Bind the game's IRQ clock after PIT reprogramming. NULL restores the
 * standalone diagnostics' default BIOS/PIT uclock timebase. */
void dos_video_set_tick_clock(volatile unsigned int *ticks, unsigned int rate);
/* Performance hint only. Broadcast always proves exact row equality; disable
 * planning for full-resolution views where adjacent pixels usually differ. */
void dos_video_set_repeated_pixel_hint(int enabled);
/* The inventory paints sharp pixels over a frozen view. Its next present may
 * update only changed scanlines of the displayed page. Consumed once. */
void dos_video_set_inventory_hint(void);
/* Inventory-only reuse of the presenter's idle packing scratch. Returns zero
 * on builds without the direct ET4000 menu path, which redraw normally. */
int dos_video_inventory_canvas_save(const unsigned char *source, unsigned int pitch);
int dos_video_inventory_canvas_restore(unsigned char *target, unsigned int pitch);
/* One-present proof from the UI's destination-page HUD cache. */
void dos_video_prove_hud_unchanged(int page);
void dos_video_unprove_hud(void);
#ifdef DOS_VGA_PROVEN_ROWS
/* Exact row proof for the next scale-2 view. End is exclusive.
 * A presentation consumes the proof; no later page can inherit it. */
void dos_video_prove_low_rows(unsigned int begin, unsigned int end);
void dos_video_unprove_low_rows(unsigned int begin, unsigned int end);
#ifdef DOS_VGA_PROVEN_VERTICAL
/* The producer also copied each even view row into its odd partner. */
void dos_video_prove_low_row_pairs(unsigned int begin, unsigned int end);
#endif
#endif

void video_init(void);
void video_set_palette(int index, int red, int green, int blue);
int video_in_vsync(void);
void video_wait_vsync(void);
void dos_video_restore(void);
void dos_video_present(const unsigned char *source, unsigned int pitch);
#ifdef DOS_VERY_LOW
void dos_video_complete_very_low_source(unsigned char *source, unsigned int pitch);
#endif
#ifdef DOS_LOW_PLANAR
/* Planar Low: the scale-2 renderer writes logical rows into these Mode X
 * plane rows (planes 0/2, 160 bytes per logical row). rows[y] marks logical
 * rows held there for the next presentation only. */
unsigned char *dos_video_planar_target(void);
void dos_video_planar_frame(const unsigned char *rows);
#endif
/* Diagnostic only, deliberately reads VRAM; not a rendering hot path. */
int dos_video_verify(const unsigned char *source, unsigned int pitch);
/* Diagnostic source completion is valid only for a proven raster upload. */
int dos_video_last_present_proven(void);
/* PC-speaker fallback owns PIT2; use conservative IRQ-clock page guards. */
void dos_video_set_speaker_clock(int enabled);

#endif
