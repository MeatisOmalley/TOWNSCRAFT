/* Minimal 3dfx Voodoo Graphics (SST-1) driver for the 1996 build.

   The board is programmed directly, as Glide 2 does on its behalf: PCI
   discovery, a 640x480 16-bit double-buffered video mode, palettized
   textures, triangles through the float parameter registers, fast fills,
   linear frame buffer writes and buffer swaps. Register offsets and fields
   are those of the SST-1 specification (and 86Box's emulation of it). */
#ifndef DOSCRAFT_R96_SST1_H
#define DOSCRAFT_R96_SST1_H
#include "common.h"

enum
{
	SST_STATUS=0x000,
	SST_FVERTEX_AX=0x088,SST_FVERTEX_AY=0x08c,SST_FVERTEX_BX=0x090,SST_FVERTEX_BY=0x094,
	SST_FVERTEX_CX=0x098,SST_FVERTEX_CY=0x09c,
	SST_FSTART_S=0x0b4,SST_FSTART_T=0x0b8,SST_FSTART_W=0x0bc,
	SST_FDSDX=0x0d4,SST_FDTDX=0x0d8,SST_FDWDX=0x0dc,
	SST_FDSDY=0x0f4,SST_FDTDY=0x0f8,SST_FDWDY=0x0fc,
	SST_FTRIANGLE=0x100,
	SST_FBZCOLORPATH=0x104,SST_FOGMODE=0x108,SST_ALPHAMODE=0x10c,SST_FBZMODE=0x110,
	SST_LFBMODE=0x114,SST_CLIP_LR=0x118,SST_CLIP_Y=0x11c,SST_NOP=0x120,
	SST_FASTFILL=0x124,SST_SWAP=0x128,SST_CHROMAKEY=0x134,SST_COLOR0=0x144,SST_COLOR1=0x148,
	SST_FBIINIT4=0x200,SST_BACKPORCH=0x208,SST_VIDEODIM=0x20c,SST_FBIINIT0=0x210,
	SST_FBIINIT1=0x214,SST_FBIINIT2=0x218,SST_FBIINIT3=0x21c,SST_HSYNC=0x220,SST_VSYNC=0x224,
	SST_DACDATA=0x22c,
	SST_TEXTUREMODE=0x300,SST_TLOD=0x304,SST_TDETAIL=0x308,SST_TEXBASE=0x30c,
	SST_NCC0_I0=0x334
};

/* fbzColorPath */
#define SST_CP_RGB_TEXTURE   1u
#define SST_CP_RGB_COLOR1    2u
#define SST_CP_PARAM_ADJUST  (1u<<26)   /* starts move from vertex A to its pixel center */
#define SST_CP_TEXTURE       (1u<<27)
/* fbzMode */
#define SST_ZM_CLIP          (1u<<0)
#define SST_ZM_CHROMAKEY     (1u<<1)
#define SST_ZM_RGB_WRITE     (1u<<9)
#define SST_ZM_DRAW_BACK     0x4000u
/* lfbMode: RGB565, writes and reads on the back buffer, pixel pipeline off */
#define SST_LFB_565_BACK     0x0050u
/* textureMode: perspective S/T, clamps, format, local-texture combine */
#define SST_TM_PERSP         (1u<<0)
#define SST_TM_CLAMP_S       (1u<<6)
#define SST_TM_CLAMP_T       (1u<<7)
#define SST_TM_P8            (5u<<8)
#define SST_TM_LOCAL         0x00241000u
/* status */
#define SST_BUSY             0x380u

#define R96_SCREEN_W 640
#define R96_SCREEN_H 480
#define R96_LFB_PITCH 1024      /* pixels per LFB row (2048 bytes) */

extern volatile u32 *r96_sst;    /* register space */
extern volatile u32 *r96_tex;    /* texture download space */
extern volatile u16 *r96_lfb;    /* linear frame buffer */

#ifdef R96_SST_HOOK
/* Native tests: a model of the board receives every access. */
void r96_sst_hook(int offset,u32 value);
void r96_sst_tex_hook(u32 offset,u32 value);
u32 r96_sst_read_hook(int offset);
static inline void sst_w(int offset,u32 value) { r96_sst_hook(offset,value); }
static inline void sst_f(int offset,float value)
{
	union { float f; u32 u; } bits;
	bits.f=value;
	r96_sst_hook(offset,bits.u);
}
static inline void sst_tex_w(u32 offset,u32 value) { r96_sst_tex_hook(offset,value); }
static inline u32 sst_r(int offset) { return r96_sst_read_hook(offset); }
#else
static inline void sst_w(int offset,u32 value) { r96_sst[offset>>2]=value; }
static inline void sst_f(int offset,float value) { ((volatile float *)r96_sst)[offset>>2]=value; }
/* Texture download space: lod<<17 | t<<9 | s<<1 for 8-bit texels */
static inline void sst_tex_w(u32 offset,u32 value) { r96_tex[offset>>2]=value; }
static inline u32 sst_r(int offset) { return r96_sst[offset>>2]; }
#endif

/* Finds and starts the board in 640x480x16 with Voodoo output. Nonzero
   when ready; zero when no board answers (the caller reports it). */
int r96_sst1_init(void);
/* Hands the monitor back to the VGA card. */
void r96_sst1_shutdown(void);
/* Waits until the FIFO and pixel pipeline are idle. */
void r96_sst1_idle(void);
/* Texture palette: 256 entries of 0xRRGGBB (palettized P8 textures). */
void r96_sst1_palette(const u32 *rgb);
/* 16x16 P8 texture at a 256-byte aligned TMU address, from rows of a
   source with the given stride. */
void r96_sst1_texture16(u32 address,const u8 *texels,int stride);
/* TMU base for a 16x16 texture (LOD 4) stored at address. */
u32 r96_sst1_texbase16(u32 address);
/* Shows the back buffer; vsync waits for the retrace. */
void r96_sst1_swap(int vsync);

#endif
