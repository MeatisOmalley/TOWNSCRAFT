/* Saving and loading worlds on a floppy disk in drive A.

   The disk is a 1232 KB (2HD) disk used raw: 77 cylinders, 2 heads, 8
   sectors of 1024 bytes. Version 4 alternates two complete, checked banks
   of column records and global state. The large legacy profile retains
   the original whole-disk RLE layout. Use a dedicated disk.

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
#include "mobs.h"

#define SECTOR_BYTES 1024
#define SECTORS_PER_TRACK 8
#define TRACK_BYTES (SECTOR_BYTES*SECTORS_PER_TRACK)
#define NUM_TRACKS (77*2)
#define SAVE_MAGIC 0x46435354    /* "TSCF" */
#define SAVE_VERSION 4           /* 4: checked, column-indexed terrain */
static int loadedMobs;
int save_loaded_mobs(void) { return loadedMobs; }

static u8 *trackBuf;              /* DMA buffer (conventional memory) */
static int curCyl=-1;
static int pageBufferedTrack=-1;
static int driveSelBits;
static void page_cancel(void);
static void fdc_buffer(void)
{
	if(trackBuf) return;
	u32 p=(u32)heap_alloc_low(TRACK_BYTES*2);
	if((p&0xFFFF)+TRACK_BYTES>0x10000) p=(p+0xFFFF)&~0xFFFF;
	trackBuf=(u8 *)p;
}

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
	pageBufferedTrack=-1;
	fdc_buffer();
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
	pageBufferedTrack=-1;
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

/* ---------------- Cooperative column reads during travel ---------------- */

static int pageState,pageMotor,pageTrack;
static u32 pageOffset,pageLength,pageDone,pageDeadline,pageIdle;
static u8 *pageDest;

static void page_cancel(void)
{
	if(pageState>=1 && pageState<=4) outb(0x200,0xD0);
	outb(0xAF,0x0F);
	pageState=pageMotor=0;
	pageBufferedTrack=-1;
	fdc_stop();
}

void save_stream_tick(void)
{
	if(pageMotor && (pageState==0 || pageState==5 || pageState<0) && g_ticks-pageIdle>200)
	{
		fdc_stop(); pageMotor=0;
	}
}

static void page_command(void)
{
	/* Adjacent compressed records often share a track. Consume the DMA
	   buffer before starting another seek/read (even with the motor off). */
	while(pageDone<pageLength)
	{
		pageTrack=(pageOffset+pageDone)/TRACK_BYTES;
		if(pageTrack!=pageBufferedTrack) break;
		u32 pos=(pageOffset+pageDone)%TRACK_BYTES,n=MIN(pageLength-pageDone,TRACK_BYTES-pos);
		memcpy(pageDest+pageDone,trackBuf+pos,n); pageDone+=n;
	}
	if(pageDone==pageLength) { pageState=5; pageIdle=g_ticks; return; }
	if(!pageMotor)
	{
		outb(0x20C,0); driveSelBits=0x51; outb(0x20C,driveSelBits);
		fdc_drive_control(0); pageMotor=1; pageState=1; pageDeadline=g_ticks;
		return;
	}
	fdc_drive_control(pageTrack&1);
	if(curCyl!=(pageTrack>>1))
	{
		outb(0x206,pageTrack>>1); outb(0x200,0x10); pageState=3;
	}
	else
	{
		pageBufferedTrack=-1;
		dma_setup((u32)trackBuf,TRACK_BYTES,1);
		outb(0x204,1); outb(0x200,0x90); pageState=4;
	}
	pageDeadline=g_ticks;
}

/* 0 pending, 1 complete, -1 error. Exactly one physical command at a time;
   the buffer and destination remain owned until its completion is polled. */
static int save_column_read(u32 offset,u32 length,u8 *dst)
{
	if(!length || length>COLUMN_CELLS || offset>NUM_TRACKS*TRACK_BYTES || length>NUM_TRACKS*TRACK_BYTES-offset) return -1;
	if(pageState>=1 && pageState<=4)
	{
		if(g_ticks-pageDeadline>300)
		{
			outb(0x200,0xD0); outb(0xAF,0x0F); pageState=-1; pageIdle=g_ticks;
			return -1;
		}
		if(pageState==1)
		{
			if(g_ticks-pageDeadline<50) return 0;
			if(inb(0x200)&0x80) return 0;
			outb(0x200,0); pageState=2; pageDeadline=g_ticks;
			return 0;
		}
		int st=inb(0x200);
		if(st&1) return 0;
		if(st&(pageState==4 ? 0x9C : 0x98))
		{
			outb(0xAF,0x0F); pageState=-1; pageIdle=g_ticks; return -1;
		}
		if(pageState==2) { curCyl=0; page_command(); return 0; }
		if(pageState==3) { curCyl=pageTrack>>1; page_command(); return 0; }
		outb(0xAF,0x0F);
		pageBufferedTrack=pageTrack;
		page_command();
		if(pageState!=5) return 0;
	}
	if(pageOffset==offset && pageLength==length && pageDest==dst)
	{
		if(pageState==5) { pageIdle=g_ticks; return 1; }
		if(pageState<0 && g_ticks-pageIdle<100) return -1;
	}
	fdc_buffer(); pageOffset=offset; pageLength=length; pageDone=0; pageDest=dst;
	page_command();
	return pageState==5 ? 1 : 0;
}

/* ---------------- Byte stream over tracks ---------------- */

static int streamTrack,streamPos,streamWrite,streamOk;
static int streamBase,streamLimit=NUM_TRACKS;
static u8 *writeBackup;
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
		streamOk=fdc_track(streamBase,0);
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
		streamOk=fdc_track(streamBase+streamTrack,1);
		memset(trackBuf,0,TRACK_BYTES);
	}
	++streamTrack;
	streamPos=0;
	if(streamTrack>=streamLimit)
	{
		streamOk=0;                   /* Disk full */
		return;
	}
	if(!streamWrite && streamOk)
	{
		streamOk=fdc_track(streamBase+streamTrack,0);
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
		streamOk=fdc_track(streamBase+streamTrack,1);
	}
}

static u32 stream_position(void) { return (streamBase+streamTrack)*TRACK_BYTES+streamPos; }

/* Save playback reads the protected source bank while assembling the new
   bank. Preserve its partial output track across use of the DMA buffer. */
static int save_column_source(u32 offset,u32 length,u8 *dst)
{
	memcpy(writeBackup,trackBuf,TRACK_BYTES);
	int status;
	do { status=save_column_read(offset,length,dst); } while(status==0);
	memcpy(trackBuf,writeBackup,TRACK_BYTES);
	pageBufferedTrack=-1;   /* Buffer holds output bytes again, not source data. */
	return status;
}

/* Each bank is a complete snapshot. A new header is published only after
   all payload tracks succeeded; the other bank remains untouched. */
#define BANK_TRACKS (NUM_TRACKS/2)
static u32 bankGeneration;
static int choose_bank(void)
{
	int best=-1;
	u32 generation=0;
	for(int bank=0; bank<2; ++bank)
	{
		streamBase=bank*BANK_TRACKS; streamLimit=BANK_TRACKS; stream_begin(0);
		if(!streamOk || get32()!=SAVE_MAGIC || get32()!=4) continue;
		u32 sum=get32(),length=get32(),seq=get32();
		if(length<64 || length>BANK_TRACKS*TRACK_BYTES-20) continue;
		streamSum=0;
		for(u32 i=0; i<length && streamOk; ++i) get8();
		if(streamOk && streamSum==sum && (best<0 || (int)(seq-generation)>0))
		{ best=bank; generation=seq; }
	}
	bankGeneration=generation;
	return best;
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
	int ok,version=world_cache_active() && g_W<256 ? SAVE_VERSION : 3;
	world_set_backing_reader(save_column_read);
	if(version<4 && !world_materialize_columns()) return SAVE_DISK_ERROR;
	page_cancel();
	if(!fdc_start())
	{
		fdc_stop();
		return SAVE_NO_DISK;
	}
	/* Pass 1: data with a zero checksum; pass 2: rewrite track 0 */
	int bank=version>=4 ? choose_bank() : -1;
	int source=world_backing_bank(BANK_TRACKS*TRACK_BYTES);
	if(source>=0) bank=source; /* Never overwrite the live paging source. */
	streamBase=version>=4 ? (bank==0 ? BANK_TRACKS : 0) : 0;
	streamLimit=version>=4 ? BANK_TRACKS : NUM_TRACKS;
	stream_begin(1);
	put32(version>=4 ? 0 : SAVE_MAGIC);
	put32(version);
	put32(0);                         /* Checksum placeholder */
	if(version>=4) put32(0);          /* Payload length, filled at commit */
	if(version>=4) put32(bankGeneration+1);
	streamSum=0;
	put_state();
	if(version>=4)
	{
		if(!writeBackup) writeBackup=heap_alloc_low(TRACK_BYTES);
		world_set_backing_reader(save_column_source);
		if(!world_save_columns(put32,stream_position)) streamOk=0;
		world_set_backing_reader(save_column_read);
	}
	else if(world_cache_active()) { if(!world_save_legacy_columns(put8)) streamOk=0; }
	else put_blocks();
	put_chests();
	mobs_save(put32);
	sum=streamSum;
	u32 length=streamTrack*TRACK_BYTES+streamPos-(version>=4 ? 20 : 12);
	stream_end_write();
	ok=streamOk;
	if(ok)
	{
		/* Checksum into the first track, which is read back and rewritten */
		ok=fdc_track(streamBase,0);
		if(ok)
		{
			trackBuf[8]=sum; trackBuf[9]=sum>>8; trackBuf[10]=sum>>16; trackBuf[11]=sum>>24;
			if(version>=4)
			{
				trackBuf[0]=(u8)SAVE_MAGIC; trackBuf[1]=(u8)(SAVE_MAGIC>>8); trackBuf[2]=(u8)(SAVE_MAGIC>>16); trackBuf[3]=(u8)(SAVE_MAGIC>>24);
				trackBuf[12]=length; trackBuf[13]=length>>8; trackBuf[14]=length>>16; trackBuf[15]=length>>24;
			}
			ok=fdc_track(streamBase,1);
		}
	}
	fdc_stop();
	pageState=pageMotor=0;
	if(ok && version>=4) world_commit_columns();
	return ok ? SAVE_OK : SAVE_DISK_ERROR;
}

int load_world(void)
{
	u32 sum,i,n,version;
	int k;
	loadedMobs=0;
	world_set_backing_reader(save_column_read);
	page_cancel();
	if(!fdc_start())
	{
		fdc_stop();
		return SAVE_NO_DISK;
	}
	int bank=choose_bank();
	streamBase=bank>=0 ? bank*BANK_TRACKS : 0;
	streamLimit=bank>=0 ? BANK_TRACKS : NUM_TRACKS;
	stream_begin(0);
	if(!streamOk || SAVE_MAGIC!=get32() || (version=get32())<1 || version>SAVE_VERSION)
	{
		fdc_stop();
		return streamOk ? SAVE_NOT_A_SAVE : SAVE_DISK_ERROR;
	}
	sum=get32();
	if(version>=4)
	{
		/* Verify the complete committed payload before changing world state. */
		u32 length=get32();
		get32(); /* Bank generation */
		if(length>BANK_TRACKS*TRACK_BYTES-20 || length<64)
		{ fdc_stop(); return SAVE_BAD_DATA; }
		if(bank<0)
		{
			streamSum=0;
			for(u32 j=0; j<length && streamOk; ++j) get8();
			if(!streamOk || streamSum!=sum)
			{ int err=streamOk ? SAVE_BAD_DATA : SAVE_DISK_ERROR; fdc_stop(); return err; }
			stream_begin(0); get32(); get32(); get32(); get32(); get32();
		}
	}
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
	if(version>=4)
	{
		world_load_position(g_player.body.x/FU,g_player.body.z/FU);
		if(!world_load_columns(get32,stream_position)) streamOk=0;
	}
	else
	{
		world_import_begin();
		n=(u32)g_W*g_W*WH;
		for(i=0; i<n && streamOk; )
		{
			int run=get8();
			u8 b=get8();
			if(0==run || i+run>n || BLK_ID(b)>=NUM_BLOCKS)
			{
				streamOk=0;
				break;
			}
			memset(g_blocks+i,b,run);
			i+=run;
		}
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
	if(version>=3 && streamOk)
	{
		if(!mobs_load(get32)) streamOk=0;
		else loadedMobs=1;
	}
	fdc_stop();
	if(version<4)
	{
		int imported=world_import_end(streamOk && sum==streamSum,g_player.body.x/FU,g_player.body.z/FU);
		if(streamOk && sum==streamSum && !imported) streamOk=0;
	}
	if(!streamOk)
	{
		loadedMobs=0;
		return SAVE_DISK_ERROR;
	}
	if(sum!=streamSum)
	{
		loadedMobs=0;
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
