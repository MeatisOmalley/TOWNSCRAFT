/* Native 32-bit controller regression. No real ports or disks are touched.
   gcc -m32 -O2 -fno-builtin -Isrc tests/hdd_test.c -o build/hdd_test
   Windows: build/tools/zig-windows-x86_64-0.13.0/zig.exe cc
     -target x86-windows-gnu -O2 -fno-builtin -Isrc tests/hdd_test.c
     -o build/hdd_test.exe
   Production is independently compiled without HDD_TEST. */
#define HDD_TEST
#define HDD_POLL_LIMIT 4096u
#include "../src/hdd.c"

extern int printf(const char *,...);
extern void exit(int);
volatile u32 g_ticks;
static u8 arena[65536] __attribute__((aligned(64)));
static u8 disk[64*512],buffer[24*512],other[24*512];
static u32 allocations,ports,maxPorts,irqDepth,freeMemory;
static struct
{
	int present,floating,otherId,tsugaru,removable,deviceType;
	int phase,selected,delay,stall,shortDma,badMessage,badStatus,badPhase;
	int unitAttention,sense,clocks,immediate,resetCount;
	u32 us,capacity,blockBytes,dmaAddress,dmaCount,baseCount;
	u8 mask,selector,mode,control,cdb[10],sent,status;
	u32 reads,writes,lastLba,lastCount;
} mock;
enum { M_FREE,M_SELECT,M_COMMAND,M_IN,M_OUT,M_STATUS,M_MESSAGE,M_ABSENT };

static void check(int ok,const char *why)
{
	if(!ok) { printf("FAIL: %s (error=%d, engine=%d, mock=%d)\n",
	                 why,hdd_error(),engine.phase,mock.phase); exit(1); }
}
void *heap_alloc_low(u32 size)
{
	check(size<=sizeof(arena),"bounded DMA allocation");
	++allocations;
	return arena;
}
void *heap_alloc_high(u32 size) { return heap_alloc_low(size); }
u32 heap_low_free(void) { return freeMemory; }
u32 heap_high_free(void) { return freeMemory; }
u32 hdd_test_irq_save(void) { ++irqDepth; return 0x200; }
void hdd_test_irq_restore(u32 flags)
{
	check(flags==0x200 && irqDepth==1,"interrupt flags preserved");
	--irqDepth;
}
static void set_phase(int phase)
{
	mock.phase=phase;
	mock.delay=mock.immediate ? 0 : 2;
}
static void execute(void)
{
	u8 opcode=mock.cdb[0];
	mock.status=0;
	if(opcode==0x00)
	{
		if(mock.unitAttention) { mock.unitAttention=0; mock.sense=6; mock.status=2; }
		set_phase(M_STATUS);
	}
	else if(opcode==0x2A) set_phase(M_OUT);
	else set_phase(M_IN);
}
static void put_be(u8 *p,u32 value)
{
	p[0]=(u8)(value>>24); p[1]=(u8)(value>>16);
	p[2]=(u8)(value>>8); p[3]=(u8)value;
}
static void dma_transfer(void)
{
	u8 answer[36]={0},*data=answer;
	u8 opcode=mock.cdb[0];
	u32 length=0,lba=big_endian(mock.cdb+2),count=mock.cdb[8];
	u32 i,available=(mock.dmaCount&0xFFFF)+1;
	u8 *memory=(u8 *)(unsigned long)mock.dmaAddress;
	check(mock.selector==7,"DMA setup restored previous base/channel selector");
	check(mock.mode==(mock.phase==M_OUT ? 0x48 : 0x44),"DMA direction/byte-count mode");
	check((mock.dmaAddress&0xFFFF)+available<=65536,"DMA does not cross 64KB");
	if(opcode==0x12)
	{
		const char *vendor=mock.tsugaru ? "TSUGARU" : "REALDISK";
		answer[0]=(u8)mock.deviceType;
		answer[1]=mock.removable ? 0x80 : 0;
		for(i=0; i<(mock.tsugaru ? 7u : 8u); ++i) answer[8+i]=(u8)vendor[i];
		for(i=0; i<8; ++i) answer[16+i]=(u8)"HARDDISK"[i];
		length=36;
	}
	else if(opcode==0x03)
	{
		answer[0]=0xF0; answer[2]=(u8)mock.sense; length=8;
	}
	else if(opcode==0x25)
	{
		put_be(answer,mock.tsugaru ? mock.capacity : mock.capacity-1);
		put_be(answer+4,mock.blockBytes); length=8;
	}
	else
	{
		check(opcode==0x28 || opcode==0x2A,"only READ/WRITE(10) at runtime");
		check(mock.cdb[1]==0 && mock.cdb[6]==0 && mock.cdb[7]==0 &&
		      mock.cdb[9]==0,"reserved CDB/LUN bytes zero");
		check(count && count<=24 && lba<mock.capacity && count<=mock.capacity-lba,
		      "CDB capacity checks");
		mock.lastLba=lba; mock.lastCount=count;
		length=count*512;
		check(length==available,"entire 24-sector payload uses bytes-1 DMA count");
		data=disk+(lba%40)*512;
		if(opcode==0x28) ++mock.reads;
		else ++mock.writes;
	}
	if(length>available) length=available;
	if(mock.shortDma && opcode==0x28) --length;
	for(i=0; i<length; ++i)
	{
		if(opcode==0x2A) data[i]=memory[i];
		else memory[i]=data[i];
	}
	mock.dmaCount=(mock.dmaCount-length)&0xFFFF;
	if(mock.badStatus && (opcode==0x28 || opcode==0x2A)) mock.status=2;
	set_phase(M_STATUS);
}
u16 hdd_test_inw(u16 port)
{
	++ports;
	check(port==0x26,"only free-running timer read as word");
	if(mock.clocks) { mock.us+=1000; g_ticks=mock.us/10000; }
	return (u16)mock.us;
}
u8 hdd_test_inb(u16 port)
{
	++ports;
	if(port==DMA_CHANNEL) return (1u<<(mock.selector&3))|((mock.selector&4) ? 0x10 : 0);
	if(port==DMA_MASK) return mock.mask;
	if(port==DMA_COUNT_LOW) return (u8)mock.dmaCount;
	if(port==DMA_COUNT_HIGH) return (u8)(mock.dmaCount>>8);
	if(port==SCSI_DATA)
	{
		u8 result=mock.status;
		if(mock.phase==M_STATUS) set_phase(M_MESSAGE);
		else if(mock.phase==M_MESSAGE) { result=mock.badMessage ? 4 : 0; set_phase(M_FREE); }
		else if(mock.phase==M_ABSENT) set_phase(M_FREE);
		else check(0,"no PIO payload reads");
		return result;
	}
	check(port==SCSI_CONTROL,"known port read, no shared terminal-count clear");
	if(mock.floating) return 0xFF;
	if(mock.delay) --mock.delay;
	if((mock.phase==M_IN || mock.phase==M_OUT) && !mock.stall &&
	   !mock.delay && !(mock.mask&DMA_BIT)) dma_transfer();
	if(mock.phase==M_FREE) return 0;
	if(mock.phase==M_SELECT) return SCSI_BUSY;
	if(mock.phase==M_ABSENT) return SCSI_STATUS|(mock.delay ? 0 : SCSI_REQ);
	{
		u8 phase=mock.phase==M_COMMAND ? SCSI_COMMAND : mock.phase==M_IN ? SCSI_DATA_IN :
		         mock.phase==M_OUT ? SCSI_DATA_OUT : mock.phase==M_STATUS ? SCSI_STATUS :
		         SCSI_MESSAGE;
		if(mock.badPhase && mock.cdb[0]==0x28 && mock.phase==M_IN) phase=SCSI_DATA_OUT;
		return phase|SCSI_BUSY|(mock.delay ? 0 : SCSI_REQ);
	}
}
void hdd_test_outb(u16 port,u8 value)
{
	++ports;
	if(port==DMA_MASK)
	{
		check((value&~DMA_BIT)==(mock.mask&~DMA_BIT),"only DMA channel 1 mask changed");
		mock.mask=value; return;
	}
	if(port==DMA_CHANNEL) { check(irqDepth==1,"DMA selector access serialized"); mock.selector=value; return; }
	if(port==DMA_COUNT_LOW) { mock.dmaCount=(mock.dmaCount&0xFF00)|value; return; }
	if(port==DMA_COUNT_HIGH) { mock.dmaCount=(mock.dmaCount&255)|((u32)value<<8); return; }
	if(port>=DMA_ADDRESS && port<=DMA_ADDRESS+3)
	{
		u32 shift=(port-DMA_ADDRESS)*8;
		mock.dmaAddress=(mock.dmaAddress&~(255u<<shift))|((u32)value<<shift); return;
	}
	if(port==DMA_MODE) { mock.mode=value; return; }
	if(port==SCSI_CONTROL)
	{
		if(!mock.present && !mock.otherId) return; /* Tsugaru ignores writes if disconnected. */
		if(value&1) { ++mock.resetCount; mock.selected=0; mock.sense=0; set_phase(M_FREE); return; }
		if((value&4) && !mock.selected)
		{
			mock.selected=1;
			if(mock.present) set_phase(M_SELECT);
			else { mock.status=2; set_phase(M_ABSENT); }
		}
		if(!(value&4) && mock.selected && mock.phase==M_SELECT)
		{
			mock.sent=0; set_phase(M_COMMAND);
		}
		mock.selected=(value&4)!=0; mock.control=value; return;
	}
	check(port==SCSI_DATA,"no global DMA reset or unrelated port write");
	if(mock.phase==M_COMMAND)
	{
		check(mock.sent<10,"CDB buffer bounds");
		mock.cdb[mock.sent++]=value;
		mock.delay=mock.immediate ? 0 : 2;
		if(mock.sent==(mock.cdb[0]>=0x20 ? 10 : 6)) execute();
	}
	else check(value==0x81,"select only ID0 with initiator ID7");
}
static void reset_mock(void)
{
	u32 i;
	hdd_cancel();
	for(i=0; i<sizeof(mock); ++i) ((u8 *)&mock)[i]=0;
	for(i=0; i<sizeof(engine); ++i) ((u8 *)&engine)[i]=0;
	for(i=0; i<sizeof(request); ++i) ((u8 *)&request)[i]=0;
	mock.present=mock.tsugaru=mock.clocks=1;
	mock.capacity=390625; mock.blockBytes=512;
	mock.mask=0x06; /* Channels 0 and 3 remain UNMASKED throughout the test. */
	mock.selector=7; /* Channel 3, base registers, asymmetric A1 readback. */
	sectors=0; bounce=0; allocations=0; ports=0; g_ticks=0; freeMemory=sizeof(arena);
	for(i=0; i<sizeof(disk); ++i) disk[i]=(u8)(i*13+(i>>9));
	for(i=0; i<sizeof(buffer); ++i) buffer[i]=0xCC;
}
static int transfer(u32 lba,u32 count,int write,u8 *data)
{
	u32 calls=0;
	int result;
	do
	{
		u32 before=ports,offset=request.copied;
		int phase=request.phase;
		result=hdd_transfer(lba,count,write,data);
		if(ports-before>maxPorts) maxPorts=ports-before;
		check(ports-before<=128,"bounded controller work per call");
		if(phase==REQUEST_COPY_IN || phase==REQUEST_COPY_OUT)
			check(request.copied-offset<=2048,"bounded copied bytes per call");
		check(++calls<5000,"bounded request completion");
	} while(result==0);
	return result;
}
int main(void)
{
	u32 i,before;
	int result;
	reset_mock();
	check(hdd_init()==1 && hdd_sector_count()==390625,"Tsugaru count normalization, exact 200MB");
	check(allocations==1 && mock.writes==0,"read-only probe allocates only after capacity");
	before=ports;
	check(hdd_init()==1 && ports==before,"successful init idempotent");
	check(transfer(2,24,0,buffer)==1,"24-sector asynchronous read");
	for(i=0; i<sizeof(buffer); ++i) check(buffer[i]==disk[2*512+i],"read payload exact");
	for(i=0; i<sizeof(buffer); ++i) buffer[i]=(u8)(i^0xA5);
	check(transfer(3,24,1,buffer)==1 && mock.writes==1,"24-sector write completes exactly once");
	for(i=0; i<sizeof(buffer); ++i) check(buffer[i]==disk[3*512+i],"write payload exact");
	check(hdd_transfer_sync(3,24,0,other)==1,"synchronous wrapper uses same engine");
	for(i=0; i<sizeof(buffer); ++i) check(buffer[i]==other[i],"write then read roundtrip");
	before=mock.writes;
	check(hdd_transfer(390625,1,1,buffer)==-1,"reject first LBA beyond disk");
	check(hdd_transfer(390624,2,1,buffer)==-1,"reject crossing capacity");
	check(hdd_transfer(0xFFFFFFFEu,24,1,buffer)==-1,"reject LBA overflow");
	check(hdd_transfer(0,0,1,buffer)==-1 && hdd_transfer(0,25,1,buffer)==-1,
	      "reject zero/oversized count");
	check(hdd_transfer(0,1,2,buffer)==-1 && hdd_transfer(0,1,0,0)==-1,
	      "reject invalid direction/null buffer");
	check(mock.writes==before,"invalid requests never write");
	check(transfer(390624,1,0,buffer)==1,"last real sector accepted");
	check(hdd_transfer(1,24,0,buffer)==0,"start async request");
	check(hdd_transfer(2,24,0,other)==-1 && hdd_error()==HDD_ERROR_BUSY,
	      "different request cannot replace pending request");
	check(transfer(1,24,0,buffer)==1,"original request survives busy error");
	check(hdd_transfer(1,24,1,buffer)==0,"write staging pending");
	before=mock.writes; hdd_cancel();
	check(mock.writes==before && request.phase==REQUEST_IDLE,"cancel write staging never writes");
	check(hdd_transfer(1,1,0,buffer)==0,"start cancellation during command");
	hdd_cancel();
	check((mock.mask&DMA_BIT) && mock.phase==M_FREE,"cancel masks DMA and resets bus");
	check(transfer(1,1,0,buffer)==1,"restart after cancellation");
	mock.shortDma=1;
	for(i=0; i<512; ++i) buffer[i]=0xCC;
	check(transfer(1,1,0,buffer)==-1 && hdd_error()==HDD_ERROR_DMA,"reject short DMA despite GOOD");
	for(i=0; i<512; ++i) check(buffer[i]==0xCC,"failed read does not expose unvalidated DMA payload");
	mock.shortDma=0; mock.badStatus=1;
	check(transfer(1,1,0,buffer)==-1 && hdd_error()==HDD_ERROR_STATUS,"CHECK CONDITION fails read");
	before=mock.writes;
	check(transfer(1,1,1,buffer)==-1 && mock.writes==before+1,"failed write is never retried");
	mock.badStatus=0; mock.badMessage=1;
	check(transfer(1,1,0,buffer)==-1 && hdd_error()==HDD_ERROR_PROTOCOL,"reject non-command-complete message");
	mock.badMessage=0; mock.badPhase=1;
	check(transfer(1,1,0,buffer)==-1 && hdd_error()==HDD_ERROR_PROTOCOL,"reject wrong data direction");
	mock.badPhase=0; mock.stall=1;
	check(transfer(1,1,0,buffer)==-1 && hdd_error()==HDD_ERROR_TIMEOUT,"stuck DMA times out");
	check((mock.mask&DMA_BIT) && mock.phase==M_FREE,"timeout stops DMA and resets bus");
	mock.stall=0;
	check(transfer(1,1,0,buffer)==1,"restart after timeout");
	check(hdd_transfer(1,1,0,buffer)==0,"start cancellation after DMA is armed");
	while(!engine.dmaArmed) check(hdd_transfer(1,1,0,buffer)==0,"reach pending DMA");
	hdd_cancel();
	check((mock.mask&DMA_BIT) && mock.phase==M_FREE,"cancel active DMA before buffer can be freed");
	mock.stall=1; mock.clocks=0;
	check(transfer(1,1,0,buffer)==-1 && hdd_error()==HDD_ERROR_TIMEOUT,"finite poll fallback with all clocks stopped");
	reset_mock(); mock.immediate=1;
	check(hdd_init()==1,"immediately ready controller probe");
	result=hdd_transfer(0,1,0,buffer);
	check(result==1,"bounded drain completes immediately progressing request in one call");
	reset_mock(); mock.tsugaru=0;
	check(hdd_init()==1 && hdd_sector_count()==390625,"standard last-LBA normalization");
	reset_mock(); mock.tsugaru=0; mock.capacity=1;
	check(hdd_init()==1 && hdd_sector_count()==1,"standard one-sector disk");
	reset_mock(); mock.unitAttention=1;
	check(hdd_init()==1 && mock.sense==6,"startup unit attention consumed without destructive reset");
	/* Caller latency is not target latency: after a long frame consume the
	   already-asserted BUSY/REQ or completed DMA before timing out a wait. */
	reset_mock();
	check(hdd_init()==1,"late-poll fixture initialized");
	{
		u8 cdb[10]={0x28,0,0,0,0,1,0,0,1,0};
		command_start(cdb,10,bounce,512,0);
		check(command_poll()==0 && engine.phase==ENGINE_SELECTED,"selection asserted");
		mock.us+=250000; g_ticks=mock.us/10000;
		check(command_poll()==0 && engine.phase==ENGINE_COMMAND,
		      "ready selection survives caller gap longer than 200ms");
		mock.delay=0; mock.us+=4000000; g_ticks=mock.us/10000;
		check(command_poll()==0 && engine.cdbSent==1,
		      "already asserted command REQ survives late poll beyond 3s");
		check(command_poll()==0,"real byte progress grants fresh handshake deadline");
		(void)engine_fail(HDD_ERROR_CANCELLED);
	}
	check(hdd_transfer(1,1,0,buffer)==0,"late DMA fixture starts");
	while(!engine.dmaArmed) check(hdd_transfer(1,1,0,buffer)==0,"late DMA fixture arms");
	mock.us+=4000000; g_ticks=mock.us/10000; mock.delay=0;
	check(transfer(1,1,0,buffer)==1,"completed DMA/status survives late caller beyond 3s");
	reset_mock(); mock.blockBytes=2048;
	check(hdd_init()==0 && sectors==0 && allocations==0,"reject non-512 sectors before allocation");
	reset_mock(); mock.capacity=0;
	check(hdd_init()==0 && allocations==0,"reject empty Tsugaru image");
	reset_mock(); mock.capacity=0x800001;
	check(hdd_init()==0,"reject Tsugaru 32-bit byte-offset overflow");
	reset_mock(); mock.tsugaru=0; mock.capacity=0;
	check(hdd_init()==0,"reject READ CAPACITY(16) sentinel without count overflow");
	reset_mock(); freeMemory=0;
	check(hdd_init()==0 && hdd_error()==HDD_ERROR_MEMORY && allocations==0,
	      "memory exhaustion returns unavailable without fatal allocation");
	reset_mock(); mock.removable=1;
	check(hdd_init()==0,"reject removable media");
	reset_mock(); mock.deviceType=5;
	check(hdd_init()==0,"reject CD-ROM target");
	reset_mock(); mock.present=0; mock.floating=1;
	check(hdd_init()==0 && mock.us<10000 && allocations==0,"floating controller fails immediately");
	reset_mock(); mock.present=0;
	check(hdd_init()==0 && mock.us<=201000 && allocations==0,"absent ID0 bounded to 200ms, no allocation");
	reset_mock(); mock.present=0; mock.clocks=0;
	check(hdd_init()==0 && hdd_error()==HDD_ERROR_TIMEOUT && allocations==0,
	      "absent target finite with both clocks stopped");
	reset_mock(); mock.us=65000;
	check(hdd_init()==1,"free-running microsecond timer wrap does not break probe");
	reset_mock(); mock.present=0; mock.otherId=1;
	check(hdd_init()==0 && allocations==0,"other ID connected, missing ID0 status-without-BUSY");
	reset_mock(); mock.phase=M_SELECT;
	check(hdd_init()==0 && mock.resetCount==0,"busy bus not reset or stolen");
	printf("PASS: probe/capacity, 24-sector DMA, async bounds/drain, writes, cancellation, errors, timeouts; max %u ports/call\n",maxPorts);
	return 0;
}
