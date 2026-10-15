/* 3dfx Voodoo Graphics (SST-1) bring-up and services; see r96_sst1.h.

   PCI configuration mechanism #1 (every 1996 PCI chipset) finds the board;
   DPMI maps its 16 MiB window, reached through DJGPP near pointers like the
   rest of this port's low memory. Video: 640x480 at 60 Hz from a 25 MHz
   pixel clock, two 16-bit color buffers of 640x480 and no depth buffer: the
   game's painter's order is exact, so depth tests would only cost fill.

   This follows 86Box's SST-1 model; a real board additionally needs its
   DRAM, graphics clock and DAC detection from Glide's sst1Init (see
   HARDWARE_1996.md). */
#include "r96_sst1.h"

volatile u32 *r96_sst;
volatile u32 *r96_tex;
volatile u16 *r96_lfb;

#ifndef R96_SST_HOOK
#include <dpmi.h>
#include <pc.h>
#include <sys/nearptr.h>

static int bus,device;
static u32 linearBase;

static u32 pci_read(int reg)
{
	outportl(0xcf8,0x80000000u|((u32)bus<<16)|((u32)device<<11)|(u32)(reg&0xfc));
	return inportl(0xcfc);
}

static void pci_write(int reg,u32 value)
{
	outportl(0xcf8,0x80000000u|((u32)bus<<16)|((u32)device<<11)|(u32)(reg&0xfc));
	outportl(0xcfc,value);
}

static int find_board(void)
{
	for(bus=0; bus<4; ++bus)
		for(device=0; device<32; ++device)
			if(pci_read(0)==0x0001121Au) return 1;   /* 3dfx, Voodoo Graphics */
	return 0;
}


/* DAC (ICS5342 class) register write, and a PLL word via index 4/data 5. */
static void dac_write(int reg,int value)
{
	sst_w(SST_DACDATA,((u32)reg<<8)|(u32)(value&0xff));
}

static void dac_pll(int index,u32 word)
{
	dac_write(4,index);
	dac_write(5,word&0xff);
	dac_write(5,word>>8);
}

int r96_sst1_init(void)
{
	__dpmi_meminfo map;
	u32 base;
	if(!find_board()) return 0;
	pci_write(0x04,pci_read(0x04)|2);           /* memory space on */
	base=pci_read(0x10)&0xFF000000u;
	if(!base) return 0;
	map.address=base;
	map.size=16u<<20;
	if(__dpmi_physical_address_mapping(&map)) return 0;
	if(!__djgpp_nearptr_enable()) return 0;
	linearBase=map.address;
	r96_sst=(volatile u32 *)(linearBase+__djgpp_conventional_base);
	r96_tex=(volatile u32 *)(linearBase+0x800000u+__djgpp_conventional_base);
	r96_lfb=(volatile u16 *)(linearBase+0x400000u+__djgpp_conventional_base);

	/* initEnable: writes to fbiInit and the PCI FIFO. */
	pci_write(0x40,0x3);
	sst_w(SST_FBIINIT1,(1u<<8));                /* video reset */
	sst_w(SST_FBIINIT0,(1u<<1)|(1u<<2));        /* graphics and FIFO reset */
	r96_sst1_idle();
	/* 25.06 MHz: 14.318 MHz * (19+2)/(4+2) / 2^1 */
	dac_pll(0,0x13u|(4u<<8)|(1u<<13));
	/* 640x480: 800 clocks by 525 lines */
	sst_w(SST_HSYNC,(704u<<16)|96u);
	sst_w(SST_VSYNC,(523u<<16)|2u);
	sst_w(SST_BACKPORCH,(33u<<16)|48u);
	sst_w(SST_VIDEODIM,(480u<<16)|(640u-1u));
	sst_w(SST_FBIINIT2,(150u<<11));             /* 600 KiB (150 pages) per buffer */
	sst_w(SST_FBIINIT3,0);
	sst_w(SST_FBIINIT1,(10u<<4));               /* 20 blocks of 32: 640 pixels; video running */
	sst_w(SST_FBIINIT0,1u);                     /* Voodoo drives the monitor */
	r96_sst1_idle();

	sst_w(SST_ALPHAMODE,0);
	sst_w(SST_FOGMODE,0);
	sst_w(SST_LFBMODE,SST_LFB_565_BACK);
	sst_w(SST_TEXTUREMODE,SST_TM_LOCAL|SST_TM_P8|SST_TM_PERSP);
	sst_w(SST_TLOD,16u|(16u<<6));               /* LOD 4 only: 16x16 */
	sst_w(SST_TDETAIL,0);
	sst_w(SST_CLIP_LR,R96_SCREEN_W);
	sst_w(SST_CLIP_Y,R96_SCREEN_H);
	sst_w(SST_COLOR1,0);
	/* Clear both buffers */
	sst_w(SST_FBZMODE,SST_ZM_CLIP|SST_ZM_RGB_WRITE|SST_ZM_DRAW_BACK);
	sst_w(SST_FASTFILL,0);
	r96_sst1_swap(0);
	sst_w(SST_FASTFILL,0);
	r96_sst1_idle();
	return 1;
}

void r96_sst1_shutdown(void)
{
	if(!r96_sst) return;
	r96_sst1_idle();
	sst_w(SST_FBIINIT0,0);                      /* VGA pass-through again */
	sst_w(SST_FBIINIT1,(1u<<8));
	r96_sst=0;
}
#endif

void r96_sst1_idle(void)
{
	int quiet=0;
	/* Glide reads status three times idle in a row: the FIFO may still be
	   handing a command to the pipeline between two reads. */
	while(quiet<3) quiet=(sst_r(SST_STATUS)&SST_BUSY) ? 0 : quiet+1;
}

void r96_sst1_palette(const u32 *rgb)
{
	int i;
	/* Even entries via nccTable0 I0, odd via I1; bit 31 selects the palette. */
	for(i=0; i<256; ++i)
		sst_w(SST_NCC0_I0+(i&1)*4,0x80000000u|((u32)(i>>1)<<24)|(rgb[i]&0xFFFFFFu));
}

u32 r96_sst1_texbase16(u32 address)
{
	/* The TMU finds LOD n after the larger levels of a 256x256 chain. */
	return ((address-(65536u+16384u+4096u+1024u))>>3)&0x7FFFFu;
}

void r96_sst1_texture16(u32 address,const u8 *texels,int stride)
{
	int t;
	sst_w(SST_TEXBASE,r96_sst1_texbase16(address));
	for(t=0; t<16; ++t,texels+=stride)
	{
		/* LOD 4, row t; four texels per write, at s=0,4,8,12 */
		u32 row=(4u<<17)|((u32)t<<9);
		const u32 *src=(const u32 *)texels;
		sst_tex_w(row,src[0]); sst_tex_w(row+8,src[1]);
		sst_tex_w(row+16,src[2]); sst_tex_w(row+24,src[3]);
	}
}

void r96_sst1_swap(int vsync)
{
	sst_w(SST_SWAP,vsync ? 1u : 0u);
}
