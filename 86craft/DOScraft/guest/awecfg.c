/* AWECFG: place the Sound Blaster AWE64's audio device at A220 I7 D1 H5,
   MPU-401 330 and OPL 388, the resources the game's SB driver expects.

   The 1996 machine's Award PnP BIOS already isolated the card and gave it a
   CSN at POST, but it chooses the IRQ and DMA itself. Like Creative's CTCM,
   this sends the ISA PnP initiation key, wakes the card into configuration
   state, programs logical device 0 and returns every card to Wait for Key.
   Standard ISA PnP 1.0a registers only; no other device is touched. */
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>

#define ADDRESS 0x279
#define WRITE_DATA 0xa79

static void reg(int index, int value)
{
	outportb(ADDRESS, index);
	outportb(WRITE_DATA, value);
}

static void initiation_key(void)
{
	unsigned char key = 0x6a;
	int i;
	outportb(ADDRESS, 0);
	outportb(ADDRESS, 0);
	for(i = 0; i < 32; ++i) {
		outportb(ADDRESS, key);
		key = (key >> 1) | (((key ^ (key >> 1)) & 1) << 7);
	}
}

int main(int argc, char **argv)
{
	int csn = argc > 1 ? atoi(argv[1]) : 1;
	if(csn < 1 || csn > 255) {
		fputs("usage: AWECFG [csn]\n", stderr);
		return 2;
	}
	initiation_key();
	reg(0x03, csn);          /* Wake[CSN]: this card enters configuration state */
	reg(0x07, 0);            /* logical device 0: audio */
	reg(0x30, 0);            /* deactivate while reprogramming */
	reg(0x60, 0x02); reg(0x61, 0x20);   /* SB DSP 220 */
	reg(0x62, 0x03); reg(0x63, 0x30);   /* MPU-401 330 */
	reg(0x64, 0x03); reg(0x65, 0x88);   /* OPL3 388 */
	reg(0x70, 7);    reg(0x71, 2);      /* IRQ 7, edge, active high */
	reg(0x74, 1);    reg(0x75, 5);      /* DMA 1 and 5 */
	reg(0x30, 1);            /* activate */
	reg(0x02, 0x02);         /* Wait for Key; CSNs are kept */
	puts("AWE64 audio: A220 I7 D1 H5 P330, OPL 388");
	return 0;
}
