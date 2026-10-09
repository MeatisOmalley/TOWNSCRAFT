#ifndef DOSCRAFT_VGA_PACK_H
#define DOSCRAFT_VGA_PACK_H

/* Transport only: preserve every indexed pixel in the Towns 320x240 layout. */
#define DOS_VGA_WIDTH 320
#define DOS_VGA_HEIGHT 240
#define DOS_VGA_PLANE_BYTES (DOS_VGA_WIDTH / 4 * DOS_VGA_HEIGHT)
int dos_vga_pack_plane(const unsigned char *source, unsigned int pitch,
                       unsigned char *destination, unsigned int capacity,
                       unsigned int plane);

#endif
