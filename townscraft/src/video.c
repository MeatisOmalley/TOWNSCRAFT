/* Video mode setup: 320x240, 256 colors, single page. */
#include "hw.h"
#include "video.h"

/* TBIOS screen mode 12 (640x480 256 colors, single page) as captured in
   experiments/crtc/settings.cpp, with ZOOM set to 2x2 for 320x240 and LO0
   halved to 0x40 for 512-byte lines, so three pages fit in VRAM. */
static const u16 crtcMode[32]=
{
	0x0060,0x02C0,0x0000,0x0000,0x031F,0x0000,0x0004,0x0000,
	0x0419,0x008A,0x030A,0x008A,0x030A,0x0046,0x0406,0x0046,
	0x0406,0x0000,0x008A,0x0000,0x0040,0x0000,0x008A,0x0000,
	0x0080,0x0058,0x0001,0x1111,0x800F,0x0002,0x0000,0x0192,
};

void video_init(void)
{
	int i;
	outb(0x448,0);
	outb(0x44A,0x08);   /* Single-page mode, page visible */
	outb(0x448,1);
	outb(0x44A,0x38);   /* 256-color palette selected */
	for(i=0; i<32; ++i)
	{
		outb(0x440,i);
		outb(0x442,crtcMode[i]&0xFF);
		outb(0x443,crtcMode[i]>>8);
	}
	outb(0xFDA0,0x0F);  /* Video output on */
	outb(0x458,0);      /* VRAM write mask: all bits writable */
	outw(0x45A,0xFFFF);
	outb(0x458,1);
	outw(0x45A,0xFFFF);
}

void video_set_palette(int idx,int r,int g,int b)
{
	outb(0xFD90,idx);
	outb(0xFD92,b);
	outb(0xFD94,r);
	outb(0xFD96,g);
}

int video_in_vsync(void)
{
	return inb(0xFDA0)&1;
}

void video_wait_vsync(void)
{
	while(video_in_vsync());
	while(!video_in_vsync());
}
