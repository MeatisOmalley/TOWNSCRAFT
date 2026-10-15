/* Delayed, single-owner SCSI fixture; the including test provides check(). */
#ifndef HDD_MOCK_ON_START
#define HDD_MOCK_ON_START() ((void)0)
#endif
#ifndef HDD_MOCK_ON_POLL
#define HDD_MOCK_ON_POLL() ((void)0)
#endif

static u8 testDisk[(1+TERRAIN_COLUMNS*TERRAIN_SECTORS*2)*512];
static int mockPresent=1,mockBusy,mockDelay,mockWrite,mockFail,mockCalls;
static u32 mockLBA,mockCount;
static u8 *mockBuffer;
int hdd_init(void) { return mockPresent; }
u32 hdd_sector_count(void) { return sizeof(testDisk)/512; }
void hdd_cancel(void) { mockBusy=0; }
int hdd_transfer(u32 lba,u32 count,int write,u8 *buffer)
{
	++mockCalls;
	check(count && lba<=sizeof(testDisk)/512 && count<=sizeof(testDisk)/512-lba,
	      "SCSI request stays in scratch area");
	if(!mockBusy)
	{
		mockLBA=lba; mockCount=count; mockWrite=write; mockBuffer=buffer;
		mockDelay=3; mockBusy=1;
		HDD_MOCK_ON_START();
	}
	else HDD_MOCK_ON_POLL();
	check(mockLBA==lba && mockCount==count && mockWrite==write && mockBuffer==buffer,
	      "pending command retains request and buffer ownership");
	if(mockDelay-->0) return 0;
	mockBusy=0;
	if(mockFail)
	{
		mockFail=0;
		if(write) memcpy(testDisk+lba*512,buffer,32); /* failed partial write */
		return -1;
	}
	if(write) memcpy(testDisk+lba*512,buffer,count*512);
	else memcpy(buffer,testDisk+lba*512,count*512);
	return 1;
}
int hdd_transfer_sync(u32 lba,u32 count,int write,u8 *buffer)
{
	int status;
	do { status=hdd_transfer(lba,count,write,buffer); } while(!status);
	return status;
}
