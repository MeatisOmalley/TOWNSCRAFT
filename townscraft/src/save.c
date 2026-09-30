/* Saving and loading worlds on a floppy disk in drive A.

   The disk is a 1232 KB (2HD) disk used raw: 77 cylinders, 2 heads, 8
   sectors of 1024 bytes.  Save data is a header (first sector) followed by
   the blocks, run-length encoded (count, block) column by column, and then
   the chests.  The whole disk is used, so it should be a dedicated disk.

   The floppy controller (MB8877 at 200h-20Eh) moves data by DMA channel 0
   (uPD71071 at A0h-AFh).  Whole tracks (8 sectors) are read and written
   with multi-sector commands; the controller is polled, not interrupt
   driven. */
#include "save.h"
#include "hw.h"
#include "sys.h"
#include "world.h"
#include "player.h"
#include "inventory.h"
#include "game.h"

#define SECTOR_BYTES 1024
#define SECTORS_PER_TRACK 8
#define TRACK_BYTES (SECTOR_BYTES*SECTORS_PER_TRACK)
#define NUM_TRACKS (77*2)
#define SAVE_MAGIC 0x46435354    /* "TSCF" */
#define SAVE_VERSION 2           /* 2: chests after the blocks */

static u8 *trackBuf;              /* DMA buffer (conventional memory) */
static int curCyl=-1;
static int driveSelBits;

/* ---------------- Floppy ---------------- */

static int fdc_wait_idle(u32 ticks)
{
	u32 t0=g_ticks;
	while(inb(0x200)&1)
	{
		if(g_ticks-t0>ticks)
		{
			return 0;
		}
	}
	return 1;
}

static void fdc_drive_control(int side)
{
	/* Bit 4 motor, bit 2 side, bit 1 double density; interrupt off */
	outb(0x208,0x12|(side ? 4 : 0));
}

static int fdc_start(void)
{
	u32 t0;
	if(!trackBuf)
	{
		/* One track; the DMA must not cross a 64 KB boundary */
		u32 p=(u32)heap_alloc_low(TRACK_BYTES*2);
		if((p&0xFFFF)+TRACK_BYTES>0x10000)
		{
			p=(p+0xFFFF)&~0xFFFF;     /* Next 64 KB boundary, still inside */
		}
		trackBuf=(u8 *)p;
	}
	outb(0x20C,0);                    /* Deselect: mode bits latch on select */
	driveSelBits=0x40|0x10|0x01;      /* High speed (2HD 1232 KB), in use, drive A */
	outb(0x20C,driveSelBits);
	fdc_drive_control(0);
	/* Motor spin-up, then wait for the drive to be ready */
	t0=g_ticks;
	while(g_ticks-t0<50);
	t0=g_ticks;
	while(inb(0x200)&0x80)
	{
		if(g_ticks-t0>200)
		{
			return 0;                 /* No disk */
		}
	}
	outb(0x200,0x00);                 /* Restore (seek to cylinder 0) */
	if(!fdc_wait_idle(300))
	{
		return 0;
	}
	curCyl=0;
	return 1;
}

static void fdc_stop(void)
{
	outb(0x208,0);                    /* Motor off */
	outb(0x20C,0);
}

static void dma_setup(u32 addr,u32 bytes,int toMemory)
{
	outb(0xAF,0x0F);                  /* Mask all */
	outb(0xA1,0x00);                  /* Channel 0, current and base */
	outb(0xA2,(bytes-1)&0xFF);
	outb(0xA3,(bytes-1)>>8);
	outb(0xA4,addr&0xFF);
	outb(0xA5,(addr>>8)&0xFF);
	outb(0xA6,(addr>>16)&0xFF);
	outb(0xA7,addr>>24);
	outb(0xAA,toMemory ? 0x44 : 0x48); /* Single transfer; device->memory or memory->device */
	outb(0xAF,0x0E);                  /* Unmask channel 0 */
}

/* Read or write one track (8 sectors) of trackBuf.  Returns 1 on success. */
static int fdc_track(int track,int write)
{
	int cyl=track>>1,side=track&1,st;
	fdc_drive_control(side);
	if(cyl!=curCyl)
	{
		outb(0x206,cyl);              /* Data register: target cylinder */
		outb(0x200,0x10);             /* Seek */
		if(!fdc_wait_idle(300))
		{
			return 0;
		}
		curCyl=cyl;
	}
	dma_setup((u32)trackBuf,TRACK_BYTES,!write);
	outb(0x204,1);                    /* First sector */
	outb(0x200,write ? 0xB0 : 0x90);  /* Multi-sector write / read */
	if(!fdc_wait_idle(300))
	{
		outb(0x200,0xD0);             /* Force interrupt */
		return 0;
	}
	outb(0xAF,0x0F);
	st=inb(0x200);
	return 0==(st&(write ? 0xFC : 0x9C));   /* Not ready, protect, fault, not found, CRC, lost */
}

/* ---------------- Byte stream over tracks ---------------- */

static int streamTrack,streamPos,streamWrite,streamOk;
static u32 streamSum;

static void stream_begin(int write)
{
	streamTrack=0;
	streamPos=0;
	streamWrite=write;
	streamOk=1;
	streamSum=0;
	if(!write)
	{
		streamOk=fdc_track(0,0);
	}
	else
	{
		memset(trackBuf,0,TRACK_BYTES);
	}
}

static void stream_next_track(void)
{
	if(streamWrite && streamOk)
	{
		streamOk=fdc_track(streamTrack,1);
		memset(trackBuf,0,TRACK_BYTES);
	}
	++streamTrack;
	streamPos=0;
	if(streamTrack>=NUM_TRACKS)
	{
		streamOk=0;                   /* Disk full */
		return;
	}
	if(!streamWrite && streamOk)
	{
		streamOk=fdc_track(streamTrack,0);
	}
	if(0==(streamTrack&3) && g_genProgress)
	{
		g_genProgress(MIN(99,streamTrack*100/40));
	}
}

static void put8(int v)
{
	if(!streamOk)
	{
		return;
	}
	if(streamPos>=TRACK_BYTES)
	{
		stream_next_track();
		if(!streamOk)
		{
			return;
		}
	}
	trackBuf[streamPos++]=(u8)v;
	streamSum=streamSum*31+(u8)v;
}

static int get8(void)
{
	u8 v;
	if(!streamOk)
	{
		return 0;
	}
	if(streamPos>=TRACK_BYTES)
	{
		stream_next_track();
		if(!streamOk)
		{
			return 0;
		}
	}
	v=trackBuf[streamPos++];
	streamSum=streamSum*31+v;
	return v;
}

static void put32(u32 v)
{
	put8(v); put8(v>>8); put8(v>>16); put8(v>>24);
}

static u32 get32(void)
{
	u32 v=get8();
	v|=(u32)get8()<<8;
	v|=(u32)get8()<<16;
	v|=(u32)get8()<<24;
	return v;
}

static void stream_end_write(void)
{
	if(streamOk && streamPos>0)
	{
		streamOk=fdc_track(streamTrack,1);
	}
}

/* ---------------- Save file ---------------- */

/* Header, then blocks.  The checksum covers everything after it and is
   written in a second pass (the header track is rewritten at the end). */
static void put_state(void)
{
	int i;
	put32(g_W);
	put32(g_player.body.x); put32(g_player.body.y); put32(g_player.body.z);
	put32(g_player.yaw); put32(g_player.pitch);
	put32(g_player.health); put32(g_player.selected);
	put32(g_player.spawnX); put32(g_player.spawnY); put32(g_player.spawnZ);
	put32(g_player.hasBedSpawn);
	put32(g_spawnX); put32(g_spawnY); put32(g_spawnZ);
	put32(g_time);
	for(i=0; i<INV_SLOTS; ++i)
	{
		put8(g_inv[i].item);
		put8(g_inv[i].count);
	}
}

static void put_blocks(void)
{
	u32 i,n=(u32)g_W*g_W*WH;
	for(i=0; i<n && streamOk; )
	{
		u8 b=g_blocks[i];
		int run=1;
		while(i+run<n && run<255 && g_blocks[i+run]==b)
		{
			++run;
		}
		put8(run);
		put8(b);
		i+=run;
	}
}

static void put_chests(void)
{
	int i,k,n=0;
	for(i=0; i<MAX_CHESTS; ++i)
	{
		n+=g_chests[i].used;
	}
	put8(n);
	for(i=0; i<MAX_CHESTS; ++i)
	{
		const Chest *c=&g_chests[i];
		if(c->used)
		{
			put8(c->x); put8(c->y); put8(c->z);
			for(k=0; k<CHEST_SLOTS; ++k)
			{
				put8(c->slot[k].item);
				put8(c->slot[k].count);
			}
		}
	}
}

int save_world(void)
{
	u32 sum;
	int ok;
	if(!fdc_start())
	{
		fdc_stop();
		return SAVE_NO_DISK;
	}
	/* Pass 1: data with a zero checksum; pass 2: rewrite track 0 */
	stream_begin(1);
	put32(SAVE_MAGIC);
	put32(SAVE_VERSION);
	put32(0);                         /* Checksum placeholder */
	streamSum=0;
	put_state();
	put_blocks();
	put_chests();
	sum=streamSum;
	stream_end_write();
	ok=streamOk;
	if(ok)
	{
		/* Checksum into the first track, which is read back and rewritten */
		ok=fdc_track(0,0);
		if(ok)
		{
			trackBuf[8]=sum; trackBuf[9]=sum>>8; trackBuf[10]=sum>>16; trackBuf[11]=sum>>24;
			ok=fdc_track(0,1);
		}
	}
	fdc_stop();
	return ok ? SAVE_OK : SAVE_DISK_ERROR;
}

int load_world(void)
{
	u32 sum,i,n,version;
	int k;
	if(!fdc_start())
	{
		fdc_stop();
		return SAVE_NO_DISK;
	}
	stream_begin(0);
	if(!streamOk || SAVE_MAGIC!=get32() || (version=get32())<1 || version>SAVE_VERSION)
	{
		fdc_stop();
		return streamOk ? SAVE_NOT_A_SAVE : SAVE_DISK_ERROR;
	}
	sum=get32();
	streamSum=0;
	if((u32)g_W!=get32())
	{
		fdc_stop();
		return SAVE_WRONG_SIZE;
	}
	g_player.body.x=get32(); g_player.body.y=get32(); g_player.body.z=get32();
	g_player.yaw=get32(); g_player.pitch=get32();
	g_player.health=get32(); g_player.selected=get32();
	g_player.spawnX=get32(); g_player.spawnY=get32(); g_player.spawnZ=get32();
	g_player.hasBedSpawn=get32();
	g_spawnX=get32(); g_spawnY=get32(); g_spawnZ=get32();
	g_time=get32();
	for(k=0; k<INV_SLOTS; ++k)
	{
		g_inv[k].item=get8();
		g_inv[k].count=get8();
	}
	n=(u32)g_W*g_W*WH;
	for(i=0; i<n && streamOk; )
	{
		int run=get8();
		u8 b=get8();
		if(0==run || i+run>n)
		{
			streamOk=0;
			break;
		}
		memset(g_blocks+i,b,run);
		i+=run;
	}
	chests_clear();
	if(version>=2)
	{
		int nc=get8(),c;
		for(c=0; c<nc && c<MAX_CHESTS && streamOk; ++c)
		{
			Chest *ch=&g_chests[c];
			ch->used=1;
			ch->x=get8(); ch->y=get8(); ch->z=get8();
			for(k=0; k<CHEST_SLOTS; ++k)
			{
				ch->slot[k].item=get8();
				ch->slot[k].count=get8();
			}
		}
	}
	fdc_stop();
	if(!streamOk)
	{
		return SAVE_DISK_ERROR;
	}
	if(sum!=streamSum)
	{
		return SAVE_BAD_DATA;
	}
	return SAVE_OK;
}

const char *save_error_text(int err)
{
	switch(err)
	{
	case SAVE_OK:         return "OK";
	case SAVE_NO_DISK:    return "No disk in drive A";
	case SAVE_DISK_ERROR: return "Disk error (write protected or full?)";
	case SAVE_NOT_A_SAVE: return "No saved world on this disk";
	case SAVE_WRONG_SIZE: return "Saved with a different memory size";
	default:              return "Saved world is damaged";
	}
}
