/* Video backend of the Voodoo build (replaces video.c). The Voodoo owns the
   monitor from video_init; the VGA card only returns for DOS at exit. The
   VGA transport hints and proofs of the Mode X presenter have nothing to
   do here and are accepted as no-ops. */
#include <stdio.h>
#include <stdlib.h>
#include "video_backend.h"
#include "r96.h"
#include "r96_sst1.h"

u32 r96_palette[256];
int r96_palette_dirty=1;
u32 r96_palette_version;
static int started;

void dos_video_restore(void)
{
	if(!started) return;
	r96_sst1_shutdown();
	started=0;
}

void video_init(void)
{
	if(started) return;
	if(!r96_sst1_init())
	{
		fputs("D963DFX needs a 3dfx Voodoo Graphics board (PCI 121A:0001).\n"
		      "Use D96SOFT.EXE without one.\n",stderr);
		exit(1);
	}
	started=1;
	atexit(dos_video_restore);
}

void video_set_palette(int index,int red,int green,int blue)
{
	u32 rgb;
	if(index<0 || index>255) return;
	rgb=((u32)(red&255)<<16)|((u32)(green&255)<<8)|(u32)(blue&255);
	if(rgb==r96_palette[index]) return;
	r96_palette[index]=rgb;
	r96_palette_dirty=1;
	++r96_palette_version;
}

/* status bit 6 is set outside the vertical retrace */
int video_in_vsync(void)
{
	return started && !(sst_r(SST_STATUS)&0x40);
}

void video_wait_vsync(void)
{
	if(!started) return;
	while(video_in_vsync());
	while(!video_in_vsync());
}

void dos_video_present(const unsigned char *source,unsigned int pitch){(void)source;(void)pitch;}
void dos_video_set_tick_clock(volatile unsigned int *ticks,unsigned int rate){(void)ticks;(void)rate;}
void dos_video_set_repeated_pixel_hint(int enabled){(void)enabled;}
void dos_video_set_inventory_hint(void){}
int dos_video_inventory_canvas_save(const unsigned char *source,unsigned int pitch){(void)source;(void)pitch;return 0;}
int dos_video_inventory_canvas_restore(unsigned char *target,unsigned int pitch){(void)target;(void)pitch;return 0;}
void dos_video_prove_hud_unchanged(int page){(void)page;}
void dos_video_unprove_hud(void){}
void dos_video_prove_low_rows(unsigned int begin,unsigned int end){(void)begin;(void)end;}
void dos_video_unprove_low_rows(unsigned int begin,unsigned int end){(void)begin;(void)end;}
void dos_video_prove_low_row_pairs(unsigned int begin,unsigned int end){(void)begin;(void)end;}
void dos_video_complete_very_low_source(unsigned char *source,unsigned int pitch){(void)source;(void)pitch;}
int dos_video_last_present_proven(void){return 0;}
int dos_video_verify(const unsigned char *source,unsigned int pitch){(void)source;(void)pitch;return 0;}
void dos_video_set_speaker_clock(int enabled){(void)enabled;}
