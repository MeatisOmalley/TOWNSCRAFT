/* Bounded compressed backing for finite legacy terrain. The same records
   are exported to the chunk-indexed floppy format. Never discard a dirty
   resident column if replacing its backing record cannot fit. */
typedef struct { u32 off,len,sum,pendingOff,pendingLen,pendingSum; u8 raw,disk,pendingRaw; } ColumnRecord;
static ColumnRecord columnRecords[256];
static u8 *columnArena,*columnScratch,*columnEncodeScratch;
static u32 columnCapacity,columnTop;
static int preparedColumn=-1;
static int (*columnRead)(u32,u32,u8 *);

static u32 column_checksum(const u8 *p,u32 n)
{
	u32 h=2166136261u;
	while(n--) h=(h^*p++)*16777619u;
	return h;
}

static void column_compact(int omit)
{
	u32 end=0,last=0;
	int done[256]={0},n;
	for(n=0; n<g_NC*g_NC; ++n)
	{
		int i,best=-1; u32 pos=0xFFFFFFFFu;
		for(i=0; i<g_NC*g_NC; ++i)
			if(i!=omit && !done[i] && !columnRecords[i].disk && columnRecords[i].len && columnRecords[i].off<pos)
			{ best=i; pos=columnRecords[i].off; }
		if(best<0) break;
		done[best]=1;
		ColumnRecord *r=&columnRecords[best];
		memmove(columnArena+end,columnArena+r->off,r->len);
		r->off=end; end+=r->len; last=end;
	}
	columnTop=last;
}

static void column_encode(int cx,int cz,u8 *dst,ColumnRecord *encoded)
{
	int i,runs=0,last=-1,n=0;
	u8 *start=dst;
	for(int cell=0; cell<CS*CS; ++cell)
	{
		const u8 *src=g_blocks+widx(cx*CS+(cell&15),0,cz*CS+(cell>>4));
		for(i=0; i<WH; ++i)
		{
			int b=src[i];
			if(b!=last || n==255) { ++runs; last=b; n=0; }
			++n;
		}
	}
	int raw=runs*2>=COLUMN_CELLS;
	u32 len=raw ? COLUMN_CELLS : runs*2;
	if(raw)
	{
		for(int cell=0; cell<CS*CS; ++cell)
		{
			memcpy(dst,g_blocks+widx(cx*CS+(cell&15),0,cz*CS+(cell>>4)),WH);
			dst+=WH;
		}
	}
	else
	{
		last=-1; n=0;
		for(int cell=0; cell<CS*CS; ++cell)
		{
			const u8 *src=g_blocks+widx(cx*CS+(cell&15),0,cz*CS+(cell>>4));
			for(i=0; i<WH; ++i)
			{
				int b=src[i];
				if(b!=last || n==255)
				{
					if(n) { *dst++=n; *dst++=last; }
					last=b; n=0;
				}
				++n;
			}
		}
		if(n) { *dst++=n; *dst++=last; }
	}
	encoded->len=len; encoded->raw=raw; encoded->sum=column_checksum(start,len);
}

static int column_capture(int cx,int cz)
{
	int id=cz*g_NC+cx;
	ColumnRecord *rec=&columnRecords[id],encoded={0};
	column_encode(cx,cz,columnEncodeScratch,&encoded);
	u32 len=encoded.len,pos,used=0;
	if(!rec->disk && rec->len>=len) pos=rec->off;
	else
	{
		if(columnTop+len>columnCapacity)
		{
			for(int i=0; i<g_NC*g_NC; ++i) if(i!=id && !columnRecords[i].disk) used+=columnRecords[i].len;
			if(used+len>columnCapacity) return 0;
			column_compact(id);
		}
		pos=columnTop; columnTop+=len;
	}
	memcpy(columnArena+pos,columnEncodeScratch,len);
	rec->off=pos; rec->len=len; rec->raw=encoded.raw; rec->disk=0; rec->sum=encoded.sum;
	return 1;
}

static int column_valid_bytes(const ColumnRecord *r,const u8 *p)
{
	u32 i,total=0;
	if(!r->len || r->len>COLUMN_CELLS || column_checksum(p,r->len)!=r->sum) return 0;
	if(r->raw)
	{
		if(r->len!=COLUMN_CELLS) return 0;
		for(i=0; i<r->len; ++i) if(BLK_ID(p[i])>=NUM_BLOCKS) return 0;
		return 1;
	}
	if(r->len&1) return 0;
	for(i=0; i<r->len; i+=2)
	{
		if(!p[i] || BLK_ID(p[i+1])>=NUM_BLOCKS) return 0;
		total+=p[i];
	}
	if(total!=COLUMN_CELLS) return 0;
	return 1;
}

static const u8 *column_bytes(int id)
{
	ColumnRecord *r=&columnRecords[id];
	if(r->disk) return preparedColumn==id ? columnScratch : NULL;
	if(r->off>columnCapacity || r->len>columnCapacity-r->off) return NULL;
	return columnArena+r->off;
}

static int column_valid(int cx,int cz)
{
	int id=cz*g_NC+cx;
	const u8 *p=column_bytes(id);
	return p && column_valid_bytes(&columnRecords[id],p);
}

static int column_prepare(int cx,int cz)
{
	int id=cz*g_NC+cx;
	ColumnRecord *r=&columnRecords[id];
	if(r->disk && preparedColumn!=id)
	{
		int status=columnRead ? columnRead(r->off,r->len,columnScratch) : -1;
		if(status!=1) { if(status<0) streamError=1; return 0; }
		preparedColumn=id;
	}
	if(!column_valid(cx,cz)) { streamError=1; return 0; }
	streamError=0;
	return 1;
}

static void column_restore_valid(int cx,int cz,u8 *dst)
{
	ColumnRecord *r=&columnRecords[cz*g_NC+cx];
	const u8 *p=column_bytes(cz*g_NC+cx);
	if(r->raw) { memcpy(dst,p,COLUMN_CELLS); return; }
	u32 i;
	for(i=0; i<r->len; i+=2) { memset(dst,p[i+1],p[i]); dst+=p[i]; }
}

static int column_restore(int cx,int cz,u8 *dst)
{
	if(!column_valid(cx,cz)) return 0;
	column_restore_valid(cx,cz,dst);
	return 1;
}
