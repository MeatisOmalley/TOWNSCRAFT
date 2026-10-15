#ifndef DOSCRAFT_F2B_CAPTURE_H
#define DOSCRAFT_F2B_CAPTURE_H
#include "raster.h"

#define DOS_F2B_COMMANDS 256
#define DOS_F2B_VERTICES 12
enum { DOS_F2B_TEXTURED, DOS_F2B_FLAT, DOS_F2B_POINT };
struct DosF2BCommand {
    int kind,count,flags,color;
    const u8 *tile;
    RVert vertices[DOS_F2B_VERTICES];
};
/* One item only, caller-owned. No allocation or retained framebuffer. Vertex
 * data is copied; atlas pointers must remain valid through replay. Reset before
 * every item. Internal fields must not be edited by the caller.
 */
struct DosF2BCapture {
    int count,overflow;
    struct DosF2BCommand commands[DOS_F2B_COMMANDS];
};
void dos_f2b_capture_reset(struct DosF2BCapture *capture);
/* Append: 1 stored, 0 capacity exhausted, -1 invalid input. Invalid input
 * preserves state; any failure requires the caller to redraw the full frame
 * with the original painter, never drop a primitive or replay a partial item.
 */
int dos_f2b_capture_poly(struct DosF2BCapture *,const RVert *,int,const u8 *,int);
int dos_f2b_capture_flat(struct DosF2BCapture *,const RVert *,int,u8);
int dos_f2b_capture_point(struct DosF2BCapture *,int,int,u8);
/* 1 emitted all commands, 0 overflow (emits nothing), -1 invalid (nothing).
 * reverse=1 reverses the EXACT emitted primitive order, not geometry depth.
 * The emitter is responsible for actual opaque/cutout coverage and sampling.
 * An overflowing capture never invokes the emitter even for the valid prefix.
 */
int dos_f2b_capture_replay(const struct DosF2BCapture *,int reverse,
    void (*emit)(const struct DosF2BCommand *,void *),void *context);
#endif
