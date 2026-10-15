/* FM TOWNS SCSI, matched to local fmtowns/src/towns/{scsi,dmac}.
   C30 data accesses acknowledge command/status/message REQ automatically.
   DATA IN/OUT are implemented ONLY through DMA by Tsugaru. Byte counts are
   bytes-1 (not words-1), matching Towns BIOS and the existing floppy driver. */
#include "hdd.h"
#include "sys.h"

#ifdef HDD_TEST
extern u8 hdd_test_inb(u16 port);
extern u16 hdd_test_inw(u16 port);
extern void hdd_test_outb(u16 port,u8 value);
extern u32 hdd_test_irq_save(void);
extern void hdd_test_irq_restore(u32 flags);
#define io_inb hdd_test_inb
#define io_inw hdd_test_inw
#define io_outb hdd_test_outb
#define irq_save hdd_test_irq_save
#define irq_restore hdd_test_irq_restore
#else
#define io_inb inb
#define io_inw inw
#define io_outb outb
static u32 irq_save(void)
{
	u32 flags;
	__asm__ volatile("pushfl; popl %0; cli":"=r"(flags)::"memory");
	return flags;
}
static void irq_restore(u32 flags)
{
	__asm__ volatile("pushl %0; popfl"::"r"(flags):"memory","cc");
}
#endif

#define SCSI_DATA 0xC30
#define SCSI_CONTROL 0xC32
#define SCSI_BUSY 0x08
#define SCSI_REQ 0x80
#define SCSI_PHASE 0x70
#define SCSI_COMMAND 0x10
#define SCSI_DATA_IN 0x40
#define SCSI_DATA_OUT 0x00
#define SCSI_STATUS 0x50
#define SCSI_MESSAGE 0x70
/* WEN enabled as in Towns BIOS. IMSK=0 means IRQ disabled in Tsugaru. */
#define CONTROL_IDLE 0x80
#define CONTROL_SELECT 0x84
#define CONTROL_DMA 0x82
#define DMA_MASK 0xAF
#define DMA_CHANNEL 0xA1
#define DMA_COUNT_LOW 0xA2
#define DMA_COUNT_HIGH 0xA3
#define DMA_ADDRESS 0xA4
#define DMA_MODE 0xAA
#define DMA_BIT 2
#define SELECT_US 200000u
#define COMMAND_US 3000000u
#define COMMAND_TICKS (3u*TICKS_PER_SEC)
#ifndef HDD_POLL_LIMIT
#define HDD_POLL_LIMIT 1000000u
#endif
#define MAX_BYTES (HDD_MAX_SECTORS*HDD_SECTOR_BYTES)
/* 16KB alignment keeps all 24 sectors within one 64KB DMA window. */
#define BOUNCE_ALIGNMENT 16384u

enum
{
	ENGINE_IDLE,ENGINE_SELECT,ENGINE_SELECTED,ENGINE_COMMAND,ENGINE_DATA,
	ENGINE_STATUS,ENGINE_MESSAGE,ENGINE_FREE
};
enum { REQUEST_IDLE,REQUEST_COPY_IN,REQUEST_COMMAND,REQUEST_COPY_OUT };

static struct
{
	int phase,write,ownsBus,dmaArmed,statusSeen;
	u8 cdb[10],cdbLength,cdbSent,status;
	u8 *data;
	u32 bytes,startTick,elapsedUs,polls;
	u16 lastTimer;
} engine;
static struct
{
	int phase,write;
	u32 lba,count,copied;
	u8 *buffer;
} request;
static u32 sectors;
static int lastError;
static u8 *bounce;
/* Small probe storage costs no heap on machines without an HDD. */
static u8 probe[64] __attribute__((aligned(64)));

int hdd_error(void) { return lastError; }
u32 hdd_sector_count(void) { return sectors; }

/* A1 readback is one-hot channel + BASE in bit4; writes use channel index
   + BASE in bit2. Save/restore that asymmetry rather than echoing readback. */
static u8 dma_selector(void)
{
	u8 r=io_inb(DMA_CHANNEL),ch=0;
	if(r&2) ch=1;
	else if(r&4) ch=2;
	else if(r&8) ch=3;
	return ch|((r&0x10) ? 4 : 0);
}
static void dma_mask(void)
{
	u32 flags=irq_save();
	io_outb(DMA_MASK,io_inb(DMA_MASK)|DMA_BIT);
	irq_restore(flags);
}
static void dma_start(void)
{
	u32 flags=irq_save();
	u8 old=dma_selector();
	u32 address=(u32)(unsigned long)engine.data;
	u32 count=engine.bytes-1;
	io_outb(DMA_MASK,io_inb(DMA_MASK)|DMA_BIT);
	io_outb(DMA_CHANNEL,1); /* Current channel 1; also updates its base. */
	io_outb(DMA_COUNT_LOW,(u8)count);
	io_outb(DMA_COUNT_HIGH,(u8)(count>>8));
	io_outb(DMA_ADDRESS,(u8)address);
	io_outb(DMA_ADDRESS+1,(u8)(address>>8));
	io_outb(DMA_ADDRESS+2,(u8)(address>>16));
	io_outb(DMA_ADDRESS+3,(u8)(address>>24));
	io_outb(DMA_MODE,engine.write ? 0x48 : 0x44);
	io_outb(DMA_CHANNEL,old);
	__asm__ volatile("":::"memory");
	io_outb(SCSI_CONTROL,CONTROL_DMA);
	io_outb(DMA_MASK,io_inb(DMA_MASK)&~DMA_BIT);
	engine.dmaArmed=1;
	irq_restore(flags);
}
static int dma_complete(void)
{
	u32 flags=irq_save();
	u8 old=dma_selector();
	u16 count;
	io_outb(DMA_CHANNEL,1);
	count=io_inb(DMA_COUNT_LOW);
	count|=(u16)io_inb(DMA_COUNT_HIGH)<<8;
	io_outb(DMA_CHANNEL,old);
	irq_restore(flags);
	/* Do not read AB: it clears terminal-count flags of ALL channels. */
	return count==0xFFFF;
}
static void clock_start(void)
{
	engine.startTick=g_ticks;
	engine.lastTimer=io_inw(0x26); /* Free-running 16-bit microsecond timer. */
	engine.elapsedUs=0;
	engine.polls=0;
}
static int timed_out(int selecting)
{
	u16 now=io_inw(0x26);
	u32 delta=(u16)(now-engine.lastTimer);
	u32 us=selecting ? SELECT_US : COMMAND_US;
	u32 ticks=selecting ? TICKS_PER_SEC/5u : COMMAND_TICKS;
	engine.lastTimer=now;
	engine.elapsedUs+=delta;
	return engine.elapsedUs>=us || (u32)(g_ticks-engine.startTick)>=ticks ||
	       ++engine.polls>=HDD_POLL_LIMIT;
}
static int engine_fail(int error)
{
	lastError=error;
	if(engine.ownsBus)
	{
		dma_mask();
		io_outb(SCSI_CONTROL,CONTROL_IDLE|1); /* Abort our bus/DMA request. */
		io_outb(SCSI_CONTROL,CONTROL_IDLE);
	}
	engine.phase=ENGINE_IDLE;
	engine.ownsBus=engine.dmaArmed=0;
	return -1;
}
static void command_start(const u8 *cdb,u8 length,u8 *data,u32 bytes,int write)
{
	u32 i;
	for(i=0; i<length; ++i) engine.cdb[i]=cdb[i];
	engine.cdbLength=length;
	engine.cdbSent=0;
	engine.data=data;
	engine.bytes=bytes;
	engine.write=write;
	engine.status=0;
	engine.statusSeen=engine.ownsBus=engine.dmaArmed=0;
	engine.phase=ENGINE_SELECT;
	clock_start();
}

/* One phase step, never a hardware wait loop. */
static int command_poll(void)
{
	u8 s,phase;
	int expired;
	if(engine.phase==ENGINE_IDLE) return -1;
	/* A cooperative caller may return after a long frame. The target may
	   already be ready (especially immediate selection), so consume visible
	   progress before applying the deadline to an actual hardware wait. */
	expired=timed_out(engine.phase==ENGINE_SELECT || engine.phase==ENGINE_SELECTED);
	s=io_inb(SCSI_CONTROL);
	if(s==0xFF) return engine_fail(HDD_ERROR_ABSENT);
	if(s&1) return engine_fail(HDD_ERROR_PROTOCOL); /* Parity error. */
	phase=s&SCSI_PHASE;
	switch(engine.phase)
	{
	case ENGINE_SELECT:
		if(s&SCSI_BUSY) return engine_fail(HDD_ERROR_BUSY);
		dma_mask();
		engine.ownsBus=1;
		io_outb(SCSI_CONTROL,CONTROL_IDLE);
		io_outb(SCSI_DATA,0x81); /* Initiator 7, only target 0. */
		io_outb(SCSI_CONTROL,CONTROL_SELECT);
		engine.phase=ENGINE_SELECTED;
		clock_start();
		return 0;
	case ENGINE_SELECTED:
		if(s&SCSI_BUSY)
		{
			io_outb(SCSI_CONTROL,CONTROL_IDLE); /* SEL falling edge -> COMMAND. */
			engine.phase=ENGINE_COMMAND;
			clock_start();
			return 0;
		}
		/* Tsugaru's missing ID returns CHECK CONDITION then BUSFREE,
		   without ever asserting BUSY or entering COMMAND. */
		if(phase==SCSI_STATUS && (s&SCSI_REQ))
		{
			(void)io_inb(SCSI_DATA);
			return engine_fail(HDD_ERROR_ABSENT);
		}
		return expired ? engine_fail(HDD_ERROR_TIMEOUT) : 0;
	case ENGINE_COMMAND:
		if(!(s&SCSI_BUSY)) return engine_fail(HDD_ERROR_PROTOCOL);
		if(!(s&SCSI_REQ)) return expired ? engine_fail(HDD_ERROR_TIMEOUT) : 0;
		if(phase==SCSI_STATUS)
		{
			engine.phase=ENGINE_STATUS; /* Early target rejection. */
			clock_start();
			return 0;
		}
		if(phase!=SCSI_COMMAND || engine.cdbSent>=engine.cdbLength)
			return engine_fail(HDD_ERROR_PROTOCOL);
		io_outb(SCSI_DATA,engine.cdb[engine.cdbSent++]);
		if(engine.cdbSent==engine.cdbLength)
			engine.phase=engine.bytes ? ENGINE_DATA : ENGINE_STATUS;
		clock_start();
		return 0;
	case ENGINE_DATA:
		if(!(s&SCSI_BUSY)) return engine_fail(HDD_ERROR_PROTOCOL);
		if(phase==SCSI_STATUS)
		{
			engine.phase=ENGINE_STATUS;
			clock_start();
			return 0;
		}
		if(!(s&SCSI_REQ)) return expired ? engine_fail(HDD_ERROR_TIMEOUT) : 0;
		if(phase!=(engine.write ? SCSI_DATA_OUT : SCSI_DATA_IN))
			return engine_fail(HDD_ERROR_PROTOCOL);
		if(!engine.dmaArmed) { dma_start(); clock_start(); return 0; }
		return expired ? engine_fail(HDD_ERROR_TIMEOUT) : 0;
	case ENGINE_STATUS:
		if(!(s&SCSI_BUSY)) return engine_fail(HDD_ERROR_PROTOCOL);
		if(!(s&SCSI_REQ)) return expired ? engine_fail(HDD_ERROR_TIMEOUT) : 0;
		if(phase!=SCSI_STATUS) return engine_fail(HDD_ERROR_PROTOCOL);
		engine.status=io_inb(SCSI_DATA);
		engine.statusSeen=1;
		/* Freeze payload before validating length or consuming message. */
		dma_mask();
		engine.phase=ENGINE_MESSAGE;
		clock_start();
		return 0;
	case ENGINE_MESSAGE:
		if(!(s&SCSI_BUSY)) return engine_fail(HDD_ERROR_PROTOCOL);
		if(!(s&SCSI_REQ)) return expired ? engine_fail(HDD_ERROR_TIMEOUT) : 0;
		if(phase!=SCSI_MESSAGE || io_inb(SCSI_DATA)!=0)
			return engine_fail(HDD_ERROR_PROTOCOL);
		engine.phase=ENGINE_FREE;
		clock_start();
		return 0;
	case ENGINE_FREE:
		if(s&SCSI_BUSY)
		{
			if(s&SCSI_REQ) return engine_fail(HDD_ERROR_PROTOCOL);
			return expired ? engine_fail(HDD_ERROR_TIMEOUT) : 0;
		}
		if(!engine.statusSeen) return engine_fail(HDD_ERROR_PROTOCOL);
		if(engine.status)
		{
			/* Normal CHECK CONDITION completed and released the bus. A reset
			   here would erase the sense data needed by REQUEST SENSE. */
			io_outb(SCSI_CONTROL,CONTROL_IDLE);
			engine.ownsBus=engine.dmaArmed=0;
			return engine_fail(HDD_ERROR_STATUS);
		}
		if(engine.cdbSent!=engine.cdbLength)
			return engine_fail(HDD_ERROR_PROTOCOL);
		if(engine.bytes && (!engine.dmaArmed || !dma_complete()))
			return engine_fail(HDD_ERROR_DMA);
		io_outb(SCSI_CONTROL,CONTROL_IDLE);
		__asm__ volatile("":::"memory");
		engine.phase=ENGINE_IDLE;
		engine.ownsBus=engine.dmaArmed=0;
		return 1;
	}
	return engine_fail(HDD_ERROR_PROTOCOL);
}
static int probe_command(u8 opcode,u8 length,u32 bytes)
{
	u8 cdb[10]={0};
	int result;
	cdb[0]=opcode;
	if(opcode==0x12 || opcode==0x03) cdb[4]=(u8)bytes;
	command_start(cdb,length,probe,bytes,0);
	do { result=command_poll(); } while(result==0);
	return result;
}
static u32 big_endian(const u8 *p)
{
	return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static int is_tsugaru(void)
{
	static const char vendor[]="TSUGARU";
	static const char product[]="HARDDISK";
	u32 i;
	for(i=0; i<7; ++i) if(probe[8+i]!=(u8)vendor[i]) return 0;
	if(probe[15]!=0 && probe[15]!=' ') return 0;
	for(i=0; i<8; ++i) if(probe[16+i]!=(u8)product[i]) return 0;
	return 1;
}
int hdd_init(void)
{
	u32 value,allocation=MAX_BYTES+BOUNCE_ALIGNMENT-1u;
	int tsugaru;
	if(sectors) return 1;
	if(request.phase!=REQUEST_IDLE) { lastError=HDD_ERROR_BUSY; return 0; }
	lastError=HDD_ERROR_NONE;
	if(probe_command(0x12,6,36)!=1) return 0;
	if(probe[0]!=0 || (probe[1]&0x80))
	{
		lastError=HDD_ERROR_CAPACITY;
		return 0;
	}
	tsugaru=is_tsugaru();
	if(probe_command(0x00,6,0)!=1)
	{
		/* Consume a single startup UNIT ATTENTION; never retry runtime writes. */
		if(lastError!=HDD_ERROR_STATUS || engine.status!=2 ||
		   probe_command(0x03,6,8)!=1 || (probe[2]&15)!=6 ||
		   probe_command(0x00,6,0)!=1) return 0;
	}
	if(probe_command(0x25,10,8)!=1) return 0;
	value=big_endian(probe);
	if(big_endian(probe+4)!=HDD_SECTOR_BYTES || value==0xFFFFFFFFu ||
	   (tsugaru && (value==0 || value>0x800000u)))
	{
		/* Local Tsugaru also multiplies HDD LBA by 512 in a 32-bit integer;
		   reject images beyond 4GiB rather than risk wrapped writes. */
		lastError=HDD_ERROR_CAPACITY;
		return 0;
	}
	if(!tsugaru) ++value;
	if(!bounce)
	{
		u8 *raw;
		/* Guard allocators: they fatal() on exhaustion rather than returning NULL. */
		if(heap_low_free()>=allocation) raw=heap_alloc_low(allocation);
		else if(heap_high_free()>=allocation) raw=heap_alloc_high(allocation);
		else { lastError=HDD_ERROR_MEMORY; return 0; }
		bounce=(u8 *)(((unsigned long)raw+BOUNCE_ALIGNMENT-1u)&
		                  ~(unsigned long)(BOUNCE_ALIGNMENT-1u));
	}
	sectors=value;
	lastError=HDD_ERROR_NONE;
	return 1;
}
static void transfer_command(void)
{
	u8 cdb[10]={0};
	cdb[0]=request.write ? 0x2A : 0x28;
	cdb[2]=(u8)(request.lba>>24);
	cdb[3]=(u8)(request.lba>>16);
	cdb[4]=(u8)(request.lba>>8);
	cdb[5]=(u8)request.lba;
	cdb[8]=(u8)request.count;
	command_start(cdb,10,bounce,request.count*HDD_SECTOR_BYTES,request.write);
	request.phase=REQUEST_COMMAND;
}
static int transfer_step(u32 lba,u32 count,int write,u8 *buffer)
{
	u32 i,bytes;
	int result;
	if(request.phase!=REQUEST_IDLE)
	{
		if(lba!=request.lba || count!=request.count || write!=request.write ||
		   buffer!=request.buffer)
		{
			lastError=HDD_ERROR_BUSY;
			return -1;
		}
	}
	else
	{
		if(!sectors) { lastError=HDD_ERROR_ABSENT; return -1; }
		if(!buffer || !count || count>HDD_MAX_SECTORS || (write!=0 && write!=1))
		{
			lastError=HDD_ERROR_ARGUMENT;
			return -1;
		}
		/* Subtraction avoids lba+count wrapping past the capacity guard. */
		if(lba>=sectors || count>sectors-lba)
		{
			lastError=HDD_ERROR_CAPACITY;
			return -1;
		}
		request.lba=lba;
		request.count=count;
		request.write=write;
		request.buffer=buffer;
		request.copied=0;
		lastError=HDD_ERROR_NONE;
		if(write) request.phase=REQUEST_COPY_IN;
		else transfer_command();
		return 0;
	}
	bytes=request.count*HDD_SECTOR_BYTES;
	if(request.phase==REQUEST_COPY_IN || request.phase==REQUEST_COPY_OUT)
	{
		u32 end=request.copied+HDD_SECTOR_BYTES;
		if(request.phase==REQUEST_COPY_IN)
			for(i=request.copied; i<end; ++i) bounce[i]=buffer[i];
		else
			for(i=request.copied; i<end; ++i) buffer[i]=bounce[i];
		request.copied=end;
		if(end==bytes)
		{
			if(request.phase==REQUEST_COPY_IN) transfer_command();
			else { request.phase=REQUEST_IDLE; lastError=HDD_ERROR_NONE; return 1; }
		}
		return 0;
	}
	result=command_poll();
	if(result<0) { request.phase=REQUEST_IDLE; return -1; }
	if(result==0) return 0;
	if(write) { request.phase=REQUEST_IDLE; lastError=HDD_ERROR_NONE; return 1; }
	request.phase=REQUEST_COPY_OUT;
	request.copied=0;
	return 0;
}
int hdd_transfer(u32 lba,u32 count,int write,u8 *buffer)
{
	u32 steps,copied=0;
	for(steps=0; steps<32; ++steps)
	{
		int rp=request.phase,ep=engine.phase,dma=engine.dmaArmed;
		u8 sent=engine.cdbSent;
		u32 offset=request.copied;
		int result=transfer_step(lba,count,write,buffer);
		if(result) return result;
		if(rp==REQUEST_COPY_IN || rp==REQUEST_COPY_OUT)
			copied+=request.copied-offset;
		if(copied>=2048u) return 0;
		/* A state step that only sampled the controller made no progress.
		   Return immediately; neither REQ nor DMA readiness is spin-polled. */
		if(rp==request.phase && ep==engine.phase && dma==engine.dmaArmed &&
		   sent==engine.cdbSent && offset==request.copied) return 0;
	}
	return 0;
}
int hdd_transfer_sync(u32 lba,u32 count,int write,u8 *buffer)
{
	int result;
	do { result=hdd_transfer(lba,count,write,buffer); } while(result==0);
	return result;
}
void hdd_cancel(void)
{
	if(request.phase==REQUEST_IDLE) return;
	if(engine.phase!=ENGINE_IDLE) (void)engine_fail(HDD_ERROR_CANCELLED);
	request.phase=REQUEST_IDLE;
	lastError=HDD_ERROR_CANCELLED;
}
