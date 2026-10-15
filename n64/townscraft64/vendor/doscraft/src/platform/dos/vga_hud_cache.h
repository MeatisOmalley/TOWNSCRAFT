#ifndef DOSCRAFT_VGA_HUD_CACHE_H
#define DOSCRAFT_VGA_HUD_CACHE_H

/* One VGA plane's 320-pixel-wide, 40-row HUD: 80 * 40 bytes. */
#define DOS_VGA_HUD_CACHE_MAX_BYTES 3200u

/* Exact byte identity, with no hash or persistent state.
 * Return 0 if equal (no writes), or 1 after copying changed bytes to shadow.
 * Return -1 for either NULL pointer or bytes > DOS_VGA_HUD_CACHE_MAX_BYTES,
 * without reading or writing either buffer. With valid pointers, bytes == 0
 * returns 0 without access. Buffers may be unaligned; each must cover bytes.
 * They must be disjoint or identical, not partially overlapping.
 * The presenter owns per-page/per-plane shadows and their initialization.
 */
int dos_vga_hud_changed(const unsigned char *newhud, unsigned char *shadow,
                         unsigned int bytes);

#endif
