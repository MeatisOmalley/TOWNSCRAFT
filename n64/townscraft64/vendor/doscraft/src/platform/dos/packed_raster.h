#ifndef DOSCRAFT_PACKED_RASTER_H
#define DOSCRAFT_PACKED_RASTER_H

/* Experimental exact low-resolution raster target. Call only after the sky
 * initializes the entire doubled viewport through the original raster API.
 * Full resolution takes the unchanged path. No changes to the display/HUD.
 */
void dos_packed_begin(unsigned char *fb,int width,int height,int pitch,int scale);

#endif
