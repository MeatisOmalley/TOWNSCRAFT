/* World storage, generation, lighting and chunk meshes. */
#include "world.h"
#include "sys.h"
#include "fmath.h"
#include "render.h"
#include "mobs.h"
#include "bench.h"

int g_W,g_NC;
u8 *g_blocks,*g_light,*g_height;
int *g_zOff;
short *g_columnMap,*g_columnCoords;
u8 *g_columnReady;
int g_residentColumns,g_allocChunkCount;
Chunk *g_chunks;
u32 *g_meshPool;
u32 g_meshQuads;
u32 g_meshVersion;   /* Incremented whenever a chunk mesh changes */
static u32 poolSize,poolTop;
static int meshJobCx=-1,meshJobCy,meshJobCz,meshJobLayer;
static void mesh_cancel(void) { meshJobCx=-1; }
static int strideZ;
static short columnMapStorage[256],columnCoordsStorage[50];
static u8 *residentBlocks,*residentLights,*residentHeights;
static u8 residentDirty[25];
static u8 residentReady[25];
static int streamError;
static int cacheAllocated;
static u32 generationMark;
static int generationTemporary;
static void cache_stream(int x,int z,int budget);
static void generation_begin(void);
static int generation_finish(void);
static int cache_load(int cx,int cz,int slot);
static int cache_light_pending(void);
static int lightBudget;
static u32 genSeed;
static u32 genVersion;
#include "column_store.inc"
int g_spawnX,g_spawnY,g_spawnZ;
void (*g_genProgress)(int percent);

static void progress(int p)
{
	if(g_genProgress)
	{
		g_genProgress(p);
	}
}

const u8 g_lightOpacity[NUM_BLOCKS]=
{
	/* AIR STONE GRASS DIRT COBBLE PLANKS LOG LEAVES */
	0,15,15,15,15,15,15,1,
	/* SAND WATER GLASS BEDROCK COAL IRON CRAFT FURNACE */
	15,2,0,15,15,15,15,15,
	/* TORCH DOORL DOORU BEDF BEDH WOOL GRAVEL FLOWER */
	0,0,0,0,0,15,15,0,
	/* TALLGRASS STONEBRICK TNT CHEST GOLD DIAMOND */
	0,15,15,15,15,15,
};

static void init_hides_tab(void);

/* ---------------- Allocation ---------------- */

#define PQ_LEN 16384
#define RQ_LEN 8192
static u32 *pq,*rq;


/* Mesh pool, in quads. Keep the existing budget and view settings while
   moving terrain to fixed resident slots. */
static u32 mesh_pool_quads(void)
{
	return g_ramMB<=2 ? 16000 : 24000;
}
void world_alloc(void)
{
	int z;
	/* Stable logical extent; code growth changes headroom, not the island. */
	g_W=g_ramMB<=2 ? 96 : (g_ramMB<=4 ? 160 : (g_ramMB<=6 ? 208 : 256));
	g_NC=g_W/CS;
	strideZ=g_W*WH;
	g_residentColumns=25;
	g_allocChunkCount=(g_residentColumns+1)*NCY;
	residentBlocks=heap_alloc_high(COLUMN_CELLS*g_residentColumns*2);
	residentLights=residentBlocks+COLUMN_CELLS*g_residentColumns;
	residentHeights=heap_alloc_low((g_residentColumns+1)*CS*CS);
	columnCapacity=(g_ramMB<=2 ? 128 : (g_ramMB<=4 ? 384 : 1024))*1024u;
	columnArena=heap_alloc_high(columnCapacity);
	columnScratch=heap_alloc_low(COLUMN_CELLS);
	columnEncodeScratch=heap_alloc_low(COLUMN_CELLS);
	g_blocks=residentBlocks; g_light=residentLights; g_height=residentHeights;
	g_columnMap=columnMapStorage; g_columnCoords=columnCoordsStorage;
	g_columnReady=residentReady;
	memset(columnMapStorage,0xFF,sizeof(columnMapStorage));
	memset(columnCoordsStorage,0xFF,sizeof(columnCoordsStorage));
	memset(residentHeights,WH,(g_residentColumns+1)*CS*CS);
	g_zOff=heap_alloc_low(sizeof(int)*256);
	cacheAllocated=1;
	init_hides_tab();
	for(z=0; z<g_W; ++z)
	{
		g_zOff[z]=z*strideZ;
	}
	g_chunks=heap_alloc_low(sizeof(Chunk)*g_allocChunkCount);
	memset(g_chunks,0,sizeof(Chunk)*g_allocChunkCount);
	poolSize=mesh_pool_quads();
	g_meshPool=heap_alloc_low(poolSize*8);
	poolTop=0;
	pq=heap_alloc_low(PQ_LEN*4);
	rq=heap_alloc_low(RQ_LEN*4);
}

/* ---------------- Light ---------------- */

static u32 pqHead,pqTail,rqHead,rqTail;

static inline void pq_push(u32 i)
{
	u32 n=(pqHead+1)&(PQ_LEN-1);
	if(n!=pqTail)
	{
		pq[pqHead]=i;
		pqHead=n;
	}
}
static inline void rq_push(u32 i,u32 lvl)
{
	u32 n=(rqHead+1)&(RQ_LEN-1);
	if(n!=rqTail)
	{
		rq[rqHead]=i|(lvl<<24);
		rqHead=n;
	}
}

static int anyDirty;
static u8 faceTab[256];

static inline void dirty_layers(Chunk *c,u32 bits,int type)
{
	anyDirty=1; c->dirtyLayers|=bits;
	if(type==DIRTY_GEOMETRY || !c->dirty ||
	   (c->dirty!=DIRTY_GEOMETRY && type==DIRTY_STREAM)) c->dirty=type;
}

static inline void mark_lit_face(int i,int x,int y,int z)
{
	if(i>=0 && faceTab[g_blocks[i]])
		dirty_layers(&g_chunks[chunk_index(x/CS,y/CS,z/CS)],1u<<(y&15),DIRTY_LIGHT);
}

static inline void light_dirty(int i,int x,int y,int z)
{
	if(meshJobCx>=0 && x>=meshJobCx*CS-1 && x<=(meshJobCx+1)*CS &&
	   y>=meshJobCy*CS-1 && y<=(meshJobCy+1)*CS &&
	   z>=meshJobCz*CS-1 && z<=(meshJobCz+1)*CS) mesh_cancel();
	int rowStride=g_columnMap ? CS*WH : strideZ;
	/* Resolve slots only at a column seam; ordinary light propagation
	   overwhelmingly visits interior cells and air with no neighboring face. */
	if(x>0) mark_lit_face(!g_columnMap || (x&15)>0 ? i-WH : widx(x-1,y,z),x-1,y,z);
	if(x<g_W-1) mark_lit_face(!g_columnMap || (x&15)<15 ? i+WH : widx(x+1,y,z),x+1,y,z);
	if(y>0) mark_lit_face(i-1,x,y-1,z);
	if(y<WH-1) mark_lit_face(i+1,x,y+1,z);
	if(z>0) mark_lit_face(!g_columnMap || (z&15)>0 ? i-rowStride : widx(x,y,z-1),x,y,z-1);
	if(z<g_W-1) mark_lit_face(!g_columnMap || (z&15)<15 ? i+rowStride : widx(x,y,z+1),x,y,z+1);
}

static int trackLightDirty;

static inline int lget(u32 i,int ch)
{
	return ch ? (g_light[i]&15) : (g_light[i]>>4);
}
static inline void lset(u32 i,int ch,int v)
{
	if(ch)
	{
		g_light[i]=(g_light[i]&0xF0)|v;
	}
	else
	{
		g_light[i]=(g_light[i]&0x0F)|(v<<4);
	}
}

/* Decode index into coordinates */
static inline void idx_xyz(u32 i,int *x,int *y,int *z)
{
	if(g_columnMap)
	{
		int slot=i/COLUMN_CELLS,cell=(i%COLUMN_CELLS)/WH;
		*y=i%WH;
		*x=g_columnCoords[slot*2]*CS+(cell&15);
		*z=g_columnCoords[slot*2+1]*CS+(cell>>4);
		return;
	}
	u32 zz=i/strideZ;
	u32 r=i-zz*strideZ;
	*z=zz;
	*x=r/WH;
	*y=r-(*x)*WH;
}

static void propagate(int ch)
{
	int count=0;
	while(pqTail!=pqHead)
	{
		if(lightBudget && count++>=lightBudget) return;
		u32 i=pq[pqTail];
		int L,x,y,z,d;
		pqTail=(pqTail+1)&(PQ_LEN-1);
		L=lget(i,ch);
		if(L<=1)
		{
			continue;
		}
		idx_xyz(i,&x,&y,&z);
		for(d=0; d<6; ++d)
		{
			int nx=x,ny=y,nz=z,op,nl;
			u32 n;
			switch(d)
			{
			case 0: --nx; break;
			case 1: ++nx; break;
			case 2: --ny; break;
			case 3: ++ny; break;
			case 4: --nz; break;
			default:++nz; break;
			}
			if(!in_world(nx,ny,nz))
			{
				continue;
			}
			n=widx(nx,ny,nz);
			if((int)n<0) continue;
			op=g_lightOpacity[BLK_ID(g_blocks[n])];
			if(op>=15)
			{
				continue;
			}
			nl=(0==ch && 2==d && 15==L && 0==op) ? 15 : L-1-op;
			if(nl>lget(n,ch))
			{
				lset(n,ch,nl);
				pq_push(n);
				if(trackLightDirty)
				{
					light_dirty(n,nx,ny,nz);
				}
			}
		}
	}
}

static void remove_light(int ch,u32 start)
{
	int lvl=lget(start,ch);
	if(0==lvl)
	{
		return;
	}
	lset(start,ch,0);
	rq_push(start,lvl);
	while(rqTail!=rqHead)
	{
		u32 e=rq[rqTail],i=e&0xFFFFFF;
		int L=e>>24,x,y,z,d;
		rqTail=(rqTail+1)&(RQ_LEN-1);
		idx_xyz(i,&x,&y,&z);
		for(d=0; d<6; ++d)
		{
			int nx=x,ny=y,nz=z,nl;
			u32 n;
			switch(d)
			{
			case 0: --nx; break;
			case 1: ++nx; break;
			case 2: --ny; break;
			case 3: ++ny; break;
			case 4: --nz; break;
			default:++nz; break;
			}
			if(!in_world(nx,ny,nz))
			{
				continue;
			}
			n=widx(nx,ny,nz);
			if((int)n<0) continue;
			nl=lget(n,ch);
			if(0==nl)
			{
				continue;
			}
			if(nl<L || (0==ch && 2==d && 15==L && 15==nl))
			{
				int emit=(ch ? g_blockDef[BLK_ID(g_blocks[n])].lightEmit : 0);
				lset(n,ch,0);
				light_dirty(n,nx,ny,nz);
				rq_push(n,nl);
				if(emit)
				{
					lset(n,ch,emit);
					pq_push(n);
				}
			}
			else
			{
				pq_push(n);
			}
		}
	}
}

static void update_height(int x,int z)
{
	int y=WH-1;
	u32 base=widx(x,0,z);
	while(y>=0 && 0==g_lightOpacity[BLK_ID(g_blocks[base+y])])
	{
		--y;
	}
	g_height[height_index(x,z)]=y+1;
}

static void light_init(void)
{
	int x,z,y;
	/* Vertical sky pass */
	for(z=0; z<g_W; ++z)
	{
		for(x=0; x<g_W; ++x)
		{
			if(!world_column_ready(x/CS,z/CS)) continue;
			u32 base=widx(x,0,z);
			int L=15;
			for(y=WH-1; y>=0; --y)
			{
				int op=g_lightOpacity[BLK_ID(g_blocks[base+y])];
				if(op)
				{
					L=MAX(0,L-1-op);
					if(op>=15)
					{
						L=0;
					}
				}
				g_light[base+y]=(u8)(L<<4);
			}
			update_height(x,z);
		}
	}
	/* Horizontal spread.  Seed each column's lit cells below the tallest
	   neighbor, then flood before moving on so the queue stays small. */
	for(z=0; z<g_W; ++z)
	{
		for(x=0; x<g_W; ++x)
		{
			if(!world_column_ready(x/CS,z/CS)) continue;
			int h=world_height(x,z),mh=h;
			if(x>0) mh=MAX(mh,world_height(x-1,z));
			if(x<g_W-1) mh=MAX(mh,world_height(x+1,z));
			if(z>0) mh=MAX(mh,world_height(x,z-1));
			if(z<g_W-1) mh=MAX(mh,world_height(x,z+1));
			for(y=h; y<mh && y<WH; ++y)
			{
				pq_push(widx(x,y,z));
			}
			/* Cells under leaves/water also need to spread */
			for(y=h-1; y>=0; --y)
			{
				u32 i=widx(x,y,z);
				if((g_light[i]>>4)==0)
				{
					break;
				}
				pq_push(i);
			}
			propagate(0);
		}
	}
}

int world_light_at(int x,int y,int z)
{
	if(!in_world(x,y,z))
	{
		return y>=WH ? 0xF0 : 0;
	}
	{
		int i=widx(x,y,z);
		return i<0 ? 0 : g_light[i];
	}
}

/* ---------------- Chunk meshes ---------------- */

/* hidesTab[n]: a face next to block n is never visible (n is opaque or water) */
static u8 hidesTab[256];

static void init_hides_tab(void)
{
	int n;
	for(n=0; n<256; ++n)
	{
		hidesTab[n]=(0!=(blk_flags(n)&BF_OPAQUE)) || B_WATER==BLK_ID(n);
		faceTab[n]=(B_AIR!=n && 0==(blk_flags(n)&BF_MODEL));
	}
}

static inline int face_visible(u8 b,u8 n)
{
	u8 nf=blk_flags(n);
	if(nf&BF_OPAQUE)
	{
		return 0;
	}
	if(B_WATER==BLK_ID(n))
	{
		return 0;
	}
	if((blk_flags(b)&BF_SAMEHIDE) && BLK_ID(n)==BLK_ID(b))
	{
		return 0;
	}
	return 1;
}

#define MAX_CHUNK_QUADS 2048
static u32 tmpQuads[MAX_CHUNK_QUADS*2];
static u32 sortedQuads[MAX_CHUNK_QUADS*2];
static int nTmp,emitMax;

static inline void emit(int lx,int ly,int lz,int dir,int w,int h,u32 w1,int model)
{
	if(nTmp<emitMax)
	{
		tmpQuads[nTmp*2]=lx|(lz<<4)|(ly<<8)|(dir<<12)|(model ? MQ_MODEL : 0)|((w-1)<<16)|((h-1)<<20);
		tmpQuads[nTmp*2+1]=w1;
		++nTmp;
	}
}

/* The face keys of one layer are 16x16 grids (index lz*16+lx) with a
   bitmask per row (bit lx of m[lz]) or, for X-facing faces, per column
   (bit lz of m[lx]) telling which cells have a face.  Keys are only valid
   where the bit is set, so the grids never need clearing; the merging
   functions clear the bits they consume. */
static inline int lowest_bit(u32 v)
{
	return __builtin_ctz(v);
}

/* Greedy rectangles (top and bottom faces) */
static void greedy2d(const u16 *key,u16 *m,int ly,int dir)
{
	int lz;
	for(lz=0; lz<16; ++lz)
	{
		while(m[lz])
		{
			u32 bits=m[lz],span;
			int lx=lowest_bit(bits),w=1,h,i;
			u16 k=key[lz*16+lx];
			while(lx+w<16 && ((bits>>(lx+w))&1) && key[lz*16+lx+w]==k)
			{
				++w;
			}
			span=((1u<<w)-1)<<lx;
			m[lz]&=~span;
			for(h=1; lz+h<16; ++h)
			{
				const u16 *row=key+(lz+h)*16+lx;
				if((m[lz+h]&span)!=span)
				{
					break;
				}
				for(i=0; i<w && row[i]==k; ++i);
				if(i<w)
				{
					break;
				}
				m[lz+h]&=~span;
			}
			emit(lx,ly,lz,dir,w,h,(k-1)&0xFFFF,0);
		}
	}
}

/* Runs along z for each column lx (X-facing faces, column masks) */
static void runs1d_z(const u16 *key,u16 *m,int ly,int dir)
{
	int lx;
	for(lx=0; lx<16; ++lx)
	{
		u32 bits=m[lx];
		while(bits)
		{
			int lz=lowest_bit(bits),h=1;
			u16 k=key[lz*16+lx];
			while(lz+h<16 && ((bits>>(lz+h))&1) && key[(lz+h)*16+lx]==k)
			{
				++h;
			}
			bits&=~(((1u<<h)-1)<<lz);
			emit(lx,ly,lz,dir,1,h,(k-1)&0xFFFF,0);
		}
		m[lx]=0;
	}
}

/* Runs along x for each row lz (Z-facing faces) */
static void runs1d(const u16 *key,u16 *m,int ly,int dir)
{
	int lz;
	for(lz=0; lz<16; ++lz)
	{
		u32 bits=m[lz];
		while(bits)
		{
			int lx=lowest_bit(bits),w=1;
			u16 k=key[lz*16+lx];
			while(lx+w<16 && ((bits>>(lx+w))&1) && key[lz*16+lx+w]==k)
			{
				++w;
			}
			bits&=~(((1u<<w)-1)<<lx);
			emit(lx,ly,lz,dir,w,1,(k-1)&0xFFFF,0);
		}
		m[lz]=0;
	}
}

/* ---------------- Mesh pool ---------------- */

/* Chunk meshes live in one pool.  Each chunk owns [off,off+cap); poolUsed
   is the sum of the caps.  Space freed by a chunk is reclaimed by
   pool_compact(), which runs only when an allocation does not fit at the
   top. */
static u32 poolUsed;

static int streamX,streamZ;       /* Camera column of the last world_stream() */
static int streamReady,streamRadius;
static u32 streamVersion;

/* Squared horizontal distance from (x,z) to the nearest cell of a chunk column */
static int column_dist2(int cx,int cz,int x,int z)
{
	int x0=cx*CS,z0=cz*CS,dx=0,dz=0;
	if(x<x0) dx=x0-x; else if(x>=x0+CS) dx=x-(x0+CS-1);
	if(z<z0) dz=z0-z; else if(z>=z0+CS) dz=z-(z0+CS-1);
	return dx*dx+dz*dz;
}

static int column_meshed(int cx,int cz)
{
	int cy;
	if(!world_column_ready(cx,cz)) return 0;
	for(cy=0; cy<NCY; ++cy)
	{
		if(g_chunks[chunk_index(cx,cy,cz)].meshed)
		{
			return 1;
		}
	}
	return 0;
}

/* Drop the meshes of a chunk column; the pool space is reclaimed by the
   next pool_compact() */
static void unmesh_column(int cx,int cz)
{
	int cy;
	if(meshJobCx==cx && meshJobCz==cz) mesh_cancel();
	for(cy=0; cy<NCY; ++cy)
	{
		Chunk *c=&g_chunks[chunk_index(cx,cy,cz)];
		g_meshQuads-=c->count;
		poolUsed-=c->cap;
		c->count=0;
		c->cap=0;
		c->meshed=0;
		c->dirty=0;
		c->dirtyLayers=0;
		c->trimmed=0;
		memset(c->group,0,sizeof(c->group));
	}
	++g_meshVersion;
}

/* Free the meshed column farthest from the camera, if it is farther than
   minDist2.  Returns 0 if there is none. */
static int evict_farthest(int minDist2)
{
	int x,z,fx=-1,fz=0,fd=minDist2;
	for(z=0; z<g_NC; ++z)
	{
		for(x=0; x<g_NC; ++x)
		{
			int dd=column_dist2(x,z,streamX,streamZ);
			if(dd>fd && column_meshed(x,z))
			{
				fx=x;
				fz=z;
				fd=dd;
			}
		}
	}
	if(fx<0)
	{
		return 0;
	}
	unmesh_column(fx,fz);
	BENCH_HOOK(++g_benchOut[BO_EVICTS]);
	return 1;
}

/* Move all meshes to the bottom of the pool, giving each a little room to
   grow (up to 16 quads) when there is space for it */
static void pool_compact(void)
{
	int n=g_allocChunkCount ? g_allocChunkCount : g_NC*g_NC*NCY,i,j,nMeshed=0;
	static u16 order[16*16*NCY];
	u32 w=0,total=0,slack;
	for(i=0; i<n; ++i)
	{
		order[i]=i;
		if(g_chunks[i].cap && g_chunks[i].count)
		{
			total+=g_chunks[i].count;
			++nMeshed;
		}
	}
	BENCH_HOOK(++g_benchOut[BO_COMPACTS]);
	slack=(poolSize>total ? (poolSize-total)/(nMeshed+1) : 0);
	slack=MIN(slack,16);
	for(i=1; i<n; ++i)
	{
		u16 k=order[i];
		j=i-1;
		while(j>=0 && g_chunks[order[j]].off>g_chunks[k].off)
		{
			order[j+1]=order[j];
			--j;
		}
		order[j+1]=k;
	}
	/* First pack tightly, then grow from the end. Growing forward can
	   overwrite a later mesh's source when its old reservation had no slack. */
	for(i=0; i<n; ++i)
	{
		Chunk *c=&g_chunks[order[i]];
		if(!c->cap || !c->count) { c->off=w; c->cap=0; continue; }
		memmove(g_meshPool+w*2,g_meshPool+c->off*2,c->count*8);
		c->off=w; c->cap=c->count; w+=c->count;
	}
	poolTop=poolUsed=w+slack*nMeshed;
	w=poolTop;
	for(i=n-1; i>=0; --i)
	{
		Chunk *c=&g_chunks[order[i]];
		if(!c->cap) continue;
		c->cap=c->count+slack; w-=c->cap;
		memmove(g_meshPool+w*2,g_meshPool+c->off*2,c->count*8);
		c->off=w;
	}
}

/* Room for n quads for chunk c (in column cx,cz).  Its old space is given
   up first.  When the pool is full, columns farther from the camera than
   this one are evicted.  Returns how many quads fit (less than n only if
   nothing more can be evicted). */
static u32 mesh_alloc(Chunk *c,int cx,int cz,u32 n)
{
	u32 need=n+16;
	if(n<=c->cap)
	{
		return n;
	}
	poolUsed-=c->cap;
	c->cap=0;
	if(poolTop+need>poolSize)
	{
		int d=column_dist2(cx,cz,streamX,streamZ);
		while(poolUsed+need>poolSize && evict_farthest(d))
		{
		}
		pool_compact();
		if(poolTop+need>poolSize)
		{
			need=n;
		}
		if(poolTop+need>poolSize)
		{
			need=(poolSize>poolTop ? poolSize-poolTop : 0);
		}
	}
	c->off=poolTop;
	c->cap=need;
	poolTop+=need;
	poolUsed+=need;
	return MIN(n,need);
}

/* ---------------- Chunk meshing ---------------- */

static int popcount16(u32 v)
{
	v=v-((v>>1)&0x5555);
	v=(v&0x3333)+((v>>2)&0x3333);
	v=(v+(v>>4))&0x0F0F;
	return (v+(v>>8))&31;
}

static void rebuild_chunk(int cx,int cy,int cz,u32 layers);

static void scan_chunk_mask(int cx,int cy,int cz,u32 layers)
{
	static u16 key[6][256],mask[6][16];
	int lx,ly,lz;
	int x0=cx*CS,y0=cy*CS,z0=cz*CS;
	int rowStride=g_columnMap ? CS*WH : strideZ;
	for(ly=0; ly<CS; ++ly)
	{
		int y=y0+ly,used=0;
		if(0==((layers>>ly)&1))
		{
			continue;
		}
		for(lz=0; lz<CS; ++lz)
		{
			int z=z0+lz;
			u32 i=widx(x0,y,z)-WH;
			/* Row away from the world's bottom, top and z edges */
			int inner=(y>0 && y<WH-1 && z>0 && z<g_W-1 && (!g_columnMap || (lz>0 && lz<CS-1)));
			for(lx=0; lx<CS; ++lx)
			{
				int x=x0+lx,d;
				u8 b,f;
				const BlockDef *def;
				i+=WH;
				b=g_blocks[i];
				if(B_AIR==b)
				{
					continue;
				}
				f=blk_flags(b);
				if(f&BF_MODEL)
				{
					emit(lx,ly,lz,0,1,1,(u32)b<<16,1);
					continue;
				}
				def=&g_blockDef[BLK_ID(b)];
				if(inner && x>0 && x<g_W-1 && (!g_columnMap || (lx>0 && lx<CS-1)))
				{
					/* Away from the world's edges: all 6 neighbors exist.
					   Skip enclosed cells (the common case underground),
					   then add the exposed faces, unrolled per direction. */
					int same=(f&BF_SAMEHIDE),id=BLK_ID(b);
					if(hidesTab[g_blocks[i+1]] && hidesTab[g_blocks[i-1]] &&
					   hidesTab[g_blocks[i-WH]] && hidesTab[g_blocks[i+WH]] &&
					   hidesTab[g_blocks[i-rowStride]] && hidesTab[g_blocks[i+rowStride]])
					{
						continue;
					}
#define FACE(D,OFF) \
					{ \
						u8 nb=g_blocks[i+(OFF)]; \
						if(!hidesTab[nb] && !(same && BLK_ID(nb)==id)) \
						{ \
							key[D][lz*16+lx]=(u16)((def->tex[D]|(g_light[i+(OFF)]<<8))+1); \
							if(D<=DIR_PX) mask[D][lx]|=1<<lz; else mask[D][lz]|=1<<lx; \
							used|=1<<(D); \
						} \
					}
					FACE(DIR_PY,1)
					FACE(DIR_NY,-1)
					FACE(DIR_NX,-WH)
					FACE(DIR_PX,WH)
					FACE(DIR_NZ,-rowStride)
					FACE(DIR_PZ,rowStride)
#undef FACE
					continue;
				}
				for(d=0; d<6; ++d)
				{
					u8 nb;
					u32 ni;
					int L;
					/* World edges and bottom are hidden; the sky above is lit */
					switch(d)
					{
					case DIR_NX: if(0==x) continue; ni=widx(x-1,y,z); break;
					case DIR_PX: if(g_W-1==x) continue; ni=widx(x+1,y,z); break;
					case DIR_NY: if(0==y) continue; ni=i-1; break;
					case DIR_PY: if(WH-1==y) { ni=0; break; } ni=i+1; break;
					case DIR_NZ: if(0==z) continue; ni=widx(x,y,z-1); break;
					default:     if(g_W-1==z) continue; ni=widx(x,y,z+1); break;
					}
					if(DIR_PY==d && WH-1==y)
					{
						nb=B_AIR;
						L=0xF0;
					}
					else
					{
						if((int)ni<0) continue;
						nb=g_blocks[ni];
						if(hidesTab[nb] || ((f&BF_SAMEHIDE) && BLK_ID(nb)==BLK_ID(b)))
						{
							continue;
						}
						L=g_light[ni];
					}
					used|=1<<d;
					key[d][lz*16+lx]=(u16)((def->tex[d]|(L<<8))+1);
					if(d<=DIR_PX)
					{
						mask[d][lx]|=1<<lz;
					}
					else
					{
						mask[d][lz]|=1<<lx;
					}
				}
			}
		}
		if(used&(1<<DIR_PY)) greedy2d(key[DIR_PY],mask[DIR_PY],ly,DIR_PY);
		if(used&(1<<DIR_NY)) greedy2d(key[DIR_NY],mask[DIR_NY],ly,DIR_NY);
		if(used&(1<<DIR_NZ)) runs1d(key[DIR_NZ],mask[DIR_NZ],ly,DIR_NZ);
		if(used&(1<<DIR_PZ)) runs1d(key[DIR_PZ],mask[DIR_PZ],ly,DIR_PZ);
		if(used&(1<<DIR_NX)) runs1d_z(key[DIR_NX],mask[DIR_NX],ly,DIR_NX);
		if(used&(1<<DIR_PX)) runs1d_z(key[DIR_PX],mask[DIR_PX],ly,DIR_PX);
	}
}

static void publish_chunk_layers(int cx,int cy,int cz,u32 layers)
{
	static u16 cnt[(NGROUPS+1)*16+1];
	static u16 bkt[MAX_CHUNK_QUADS];
	Chunk *c=&g_chunks[chunk_index(cx,cy,cz)];
	int n,lx,ly,lz,g,i,nNew;
	int x0=cx*CS,y0=cy*CS,z0=cz*CS;
	int waterChanged=(0xFFFF==layers || DIRTY_GEOMETRY==c->dirty);
	u16 group[NGROUPS+1],gstart[NGROUPS+2];
	int x1[NGROUPS],z1[NGROUPS],y1[NGROUPS],xa[NGROUPS],za[NGROUPS],ya[NGROUPS];
	u32 *out;
	nNew=nTmp;
	BENCH_HOOK(++g_benchOut[BO_REBUILDS]);
	BENCH_HOOK(g_benchOut[BO_LAYERS]+=popcount16(layers));
	if(0xFFFF!=layers && nNew+c->count>MAX_CHUNK_QUADS)
	{
		/* The kept and new quads might not fit the buffers together */
		rebuild_chunk(cx,cy,cz,0xFFFF);
		return;
	}

	/* Sort the new quads by group (direction and quadrant, models last)
	   and, within a group, by plane coordinate so visible planes come
	   first: ascending for +X/+Y/+Z (visible when the camera is
	   above/after), descending for -X/-Y/-Z.  Grow the boxes of the cells
	   each group covers (exact for a whole mesh; for a partial rebuild the
	   old boxes grown by the new quads, which may be loose and only make
	   culling less tight). */
	for(g=0; g<NGROUPS; ++g)
	{
		if(0xFFFF==layers)
		{
			xa[g]=za[g]=ya[g]=15;
			x1[g]=z1[g]=y1[g]=0;
		}
		else
		{
			xa[g]=c->gbox[g][0]&15; x1[g]=c->gbox[g][0]>>4;
			za[g]=c->gbox[g][1]&15; z1[g]=c->gbox[g][1]>>4;
			ya[g]=c->gbox[g][2]&15; y1[g]=c->gbox[g][2]>>4;
		}
	}
	memset(cnt,0,sizeof(cnt));
	for(i=0; i<nNew; ++i)
	{
		u32 w0=tmpQuads[i*2];
		int k;
		if(w0&MQ_MODEL)
		{
			k=NGROUPS*16;
		}
		else
		{
			int d=MQ_DIR(w0),p,lx=MQ_LX(w0),lz=MQ_LZ(w0),ly=MQ_LY(w0);
			int ex=(d<=DIR_PX) ? 1 : MQ_W(w0),ez=(d<=DIR_PY) ? MQ_H(w0) : 1;
			g=d*NSUB+(lx>=8)+((lz>=8)<<1);
			p=(d<=DIR_PX) ? lx : (d<=DIR_PY ? ly : lz);
			if(0==(d&1))
			{
				p=15-p;
			}
			k=g*16+p;
			xa[g]=MIN(xa[g],lx); x1[g]=MAX(x1[g],lx+ex-1);
			za[g]=MIN(za[g],lz); z1[g]=MAX(z1[g],lz+ez-1);
			ya[g]=MIN(ya[g],ly); y1[g]=MAX(y1[g],ly);
		}
		bkt[i]=k;
		++cnt[k+1];
	}
	for(i=1; i<=(NGROUPS+1)*16; ++i)
	{
		cnt[i]+=cnt[i-1];
	}
	for(g=0; g<=NGROUPS; ++g)
	{
		gstart[g]=cnt[g*16];
	}
	gstart[NGROUPS+1]=nNew;
	for(i=0; i<nNew; ++i)
	{
		u16 d=cnt[bkt[i]]++;
		sortedQuads[d*2]=tmpQuads[i*2];
		sortedQuads[d*2+1]=tmpQuads[i*2+1];
	}
	if(0xFFFF==layers)
	{
		out=sortedQuads;
		for(g=0; g<=NGROUPS; ++g)
		{
			group[g]=gstart[g+1]-gstart[g];
		}
	}
	else
	{
		/* Merge each group's kept quads (already in order) with its new
		   ones.  Groups whose box has no rebuilt layer and that get no new
		   quads are copied whole. */
		const u32 *q=g_meshPool+c->off*2;
		u32 *o=tmpQuads;
		out=tmpQuads;
		for(g=0; g<=NGROUPS; ++g)
		{
			int cntOld=c->group[g],ns=gstart[g],ne=gstart[g+1];
			u32 *o0=o;
			if(g<NGROUPS && ns==ne)
			{
				int ylo=c->gbox[g][2]&15,yhi=c->gbox[g][2]>>4;
				if(0==cntOld)
				{
					group[g]=0;
					continue;
				}
				if(0==((layers>>ylo)&((2u<<(yhi-ylo))-1)))
				{
					memcpy(o,q,cntOld*8);
					o+=cntOld*2;
					q+=cntOld*2;
					group[g]=cntOld;
					continue;
				}
			}
			if(g<NGROUPS)
			{
				int d=g/NSUB,sh=(d<=DIR_PX ? 0 : (d<=DIR_PY ? 8 : 4)),flip=(0==(d&1) ? 15 : 0);
				for(i=cntOld; i>0; --i,q+=2)
				{
					u32 w0=q[0];
					int pk;
					if((layers>>MQ_LY(w0))&1)
					{
						continue;
					}
					pk=((w0>>sh)&15)^flip;
					while(ns<ne && (int)(((sortedQuads[ns*2]>>sh)&15)^flip)<pk)
					{
						o[0]=sortedQuads[ns*2];
						o[1]=sortedQuads[ns*2+1];
						o+=2;
						++ns;
					}
					o[0]=w0;
					o[1]=q[1];
					o+=2;
				}
			}
			else
			{
				/* Model cells: kept, then new */
				for(i=cntOld; i>0; --i,q+=2)
				{
					if(0==((layers>>MQ_LY(q[0]))&1))
					{
						o[0]=q[0];
						o[1]=q[1];
						o+=2;
					}
				}
			}
			for(; ns<ne; ++ns)
			{
				o[0]=sortedQuads[ns*2];
				o[1]=sortedQuads[ns*2+1];
				o+=2;
			}
			group[g]=(o-o0)/2;
		}
		nTmp=(o-tmpQuads)/2;
	}
	for(g=0; g<NGROUPS; ++g)
	{
		c->gbox[g][0]=xa[g]|(x1[g]<<4);
		c->gbox[g][1]=za[g]|(z1[g]<<4);
		c->gbox[g][2]=ya[g]|(y1[g]<<4);
	}
	g_meshQuads-=c->count;
	c->count=0;
	n=mesh_alloc(c,cx,cz,nTmp);
	c->trimmed=0;
	if(n<nTmp)
	{
		/* Out of pool space: keep what fits (trimming the group counts
		   from the end so they still add up) */
		int excess=nTmp-n;
		c->trimmed=1;
		for(g=NGROUPS; g>=0 && excess>0; --g)
		{
			int t=MIN(excess,(int)group[g]);
			group[g]-=t;
			excess-=t;
		}
	}
	memcpy(c->group,group,sizeof(group));
	++g_meshVersion;
	g_meshQuads+=n;
	memcpy(g_meshPool+c->off*2,out,n*8);
	c->count=n;
	c->dirty=0;
	c->dirtyLayers=0;
	c->meshed=1;
	/* A bottom-layer chunk is sealed when nothing can be seen into it from
	   above: its top cells are opaque wherever the cells above are not,
	   and no water touches its sides (see collect in render.c).  Only the
	   top layer can change whether it is sealed. */
	if(0==cy && ((layers&0x8000) || waterChanged))
	{
		int sealed=1,water=0;
		for(lz=0; lz<CS; ++lz)
		{
			for(lx=0; lx<CS; ++lx)
			{
				int x=x0+lx,z=z0+lz;
				u32 i=widx(x,CS-1,z);
				if(sealed && !(blk_flags(g_blocks[i])&BF_OPAQUE) && !(blk_flags(g_blocks[i+1])&BF_OPAQUE))
				{
					sealed=0;
				}
				if(waterChanged && !water && (0==lx || 0==lz || CS-1==lx || CS-1==lz))
				{
					for(ly=0; ly<CS; ++ly)
					{
						if(B_WATER==BLK_ID(g_blocks[widx(x,ly,z)]))
						{
							water=1;
							break;
						}
					}
				}
			}
		}
		if(waterChanged)
		{
			c->waterSide=water;
		}
		c->sealed=sealed && !c->waterSide;
	}
}


static void rebuild_chunk(int cx,int cy,int cz,u32 layers)
{
	Chunk *c=&g_chunks[chunk_index(cx,cy,cz)];
	mesh_cancel();
	layers&=0xFFFF;
	if(!c->meshed || c->trimmed || !layers || popcount16(layers)>=12) layers=0xFFFF;
	nTmp=0; emitMax=MAX_CHUNK_QUADS;
	scan_chunk_mask(cx,cy,cz,layers);
	publish_chunk_layers(cx,cy,cz,layers);
}

static void build_chunk(int cx,int cy,int cz) { rebuild_chunk(cx,cy,cz,0xFFFF); }

static void mark_dirty(int x,int y,int z)
{
	if(in_world(x,y,z) && world_column_ready(x/CS,z/CS))
	{
		if(meshJobCx==x/CS && meshJobCy==y/CS && meshJobCz==z/CS) mesh_cancel();
		dirty_layers(&g_chunks[chunk_index(x/CS,y/CS,z/CS)],1u<<(y&15),DIRTY_GEOMETRY);
	}
}

static void mesh_reset(void)
{
	mesh_cancel(); streamReady=0;
	memset(g_chunks,0,sizeof(Chunk)*(g_allocChunkCount ? g_allocChunkCount : g_NC*g_NC*NCY));
	poolTop=poolUsed=0; anyDirty=0; g_meshQuads=0; ++g_meshVersion;
}

/* Full new meshes/seam refreshes may be sliced. Player edits never use this. */
static void mesh_step(int cx,int cy,int cz)
{
	if(meshJobCx!=cx || meshJobCy!=cy || meshJobCz!=cz)
	{
		meshJobCx=cx; meshJobCy=cy; meshJobCz=cz; meshJobLayer=0;
		nTmp=0; emitMax=MAX_CHUNK_QUADS;
	}
	int end=MIN(meshJobLayer+4,CS);
	scan_chunk_mask(cx,cy,cz,((1u<<end)-1)^((1u<<meshJobLayer)-1));
	meshJobLayer=end;
	if(end<CS) return;
	publish_chunk_layers(cx,cy,cz,0xFFFF);
	mesh_cancel();
}

/* ---------------- Mesh streaming ---------------- */

void world_stream(int x,int z,int radius,int maxBuild)
{
	if(maxBuild>100) mesh_cancel();
	if(cacheAllocated) cache_stream(x,z,maxBuild>100 ? 100000 : 1);
	int r2=radius*radius;
	int cx0=MAX(0,(x-radius)/CS),cx1=MIN(g_NC-1,(x+radius)/CS);
	int cz0=MAX(0,(z-radius)/CS),cz1=MIN(g_NC-1,(z+radius)/CS);
	/* Once the neighborhood is complete, standing still (or just turning)
	   needs no search.  Mesh changes can evict columns, so invalidate on
	   the mesh version as well as camera position and view distance. */
	if(streamReady && x==streamX && z==streamZ && radius==streamRadius &&
	   streamVersion==g_meshVersion)
	{
		return;
	}
	streamReady=0;
	streamX=x;
	streamZ=z;
	streamRadius=radius;
	while(maxBuild-->0)
	{
		if(cacheAllocated && maxBuild<100 && meshJobCx>=0)
		{
			/* Deferred refreshes resume through world_update_dirty_chunks. */
			if(g_chunks[chunk_index(meshJobCx,meshJobCy,meshJobCz)].meshed) return;
			if(world_column_sim_ready(meshJobCx,meshJobCz) && column_dist2(meshJobCx,meshJobCz,x,z)<=r2)
			{
				mesh_step(meshJobCx,meshJobCy,meshJobCz); return;
			}
			mesh_cancel();
		}
		int cx,cz,cy,bestCx=-1,bestCz=0,bestCy=0,bd=r2+1;
		for(cz=cz0; cz<=cz1; ++cz)
		{
			for(cx=cx0; cx<=cx1; ++cx)
			{
				if(!world_column_sim_ready(cx,cz)) continue;
				int d=column_dist2(cx,cz,x,z);
				if(d<bd)
				{
					for(cy=0; cy<NCY; ++cy)
					{
						if(!g_chunks[chunk_index(cx,cy,cz)].meshed)
						{
							bestCx=cx;
							bestCz=cz;
							bestCy=cy;
							bd=d;
							break;
						}
					}
				}
			}
		}
		if(bestCx<0)
		{
			streamReady=1;
			streamVersion=g_meshVersion;
			return;
		}
		if(cacheAllocated && maxBuild<100) mesh_step(bestCx,bestCy,bestCz);
		else build_chunk(bestCx,bestCy,bestCz);
	}
}

/* Player geometry first, even while streaming light is unfinished. Light-only
   work waits for settled light; seam refreshes retain their bounded budget. */
int world_update_dirty_chunks(int maxLightOnly)
{
	int count=g_allocChunkCount ? g_allocChunkCount : g_NC*g_NC*NCY,worked=0;
	int drain=maxLightOnly>100;
	if(!anyDirty) return 0;
	for(int pass=0; pass<2; ++pass)
	for(int i=0; i<count; ++i)
	{
		Chunk *c=&g_chunks[i];
		int cx,cy=i%NCY,cz;
		if(!c->dirty) continue;
		if(!c->meshed) { c->dirty=0; c->dirtyLayers=0; continue; }
		if(g_columnMap) { cx=g_columnCoords[(i/NCY)*2]; cz=g_columnCoords[(i/NCY)*2+1]; }
		else { cx=(i/NCY)%g_NC; cz=i/(g_NC*NCY); }
		if(cx<0 || cz<0) continue;
		if(pass==0)
		{
			if(c->dirty!=DIRTY_GEOMETRY) continue;
			rebuild_chunk(cx,cy,cz,c->dirtyLayers); worked=1;
		}
		else
		{
			if((worked && !drain) || maxLightOnly<=0 || c->dirty==DIRTY_GEOMETRY ||
			   !world_column_sim_ready(cx,cz) || (cacheAllocated && cache_light_pending())) continue;
			if(c->dirty==DIRTY_STREAM && cacheAllocated && maxLightOnly<100) mesh_step(cx,cy,cz);
			else rebuild_chunk(cx,cy,cz,c->dirtyLayers);
			--maxLightOnly;
			worked=1;
		}
	}
	anyDirty=0;
	for(int i=0; i<count; ++i) if(g_chunks[i].dirty) { anyDirty=1; break; }
	return worked;
}

/* After g_blocks was loaded from a save: light (sky, then every light
   emitting block) and meshes, as at the end of world_generate() */
void world_rebuild_after_load(void)
{
	int x,y,z;
	progress(10);
	light_init();
	progress(40);
	for(z=0; z<g_W; ++z)
	{
		for(x=0; x<g_W; ++x)
		{
			if(!world_column_ready(x/CS,z/CS)) continue;
			u32 base=widx(x,0,z);
			for(y=0; y<WH; ++y)
			{
				int e=g_blockDef[BLK_ID(g_blocks[base+y])].lightEmit;
				if(e)
				{
					lset(base+y,1,e);
					pq_push(base+y);
					propagate(1);
				}
			}
		}
	}
	/* Meshes are built by world_stream() around the player */
	mesh_reset();
	progress(100);
	trackLightDirty=1;
}

/* A streamed column may still be using the shared flood-fill queue when the
   player edits a block.  Keep the edit visible immediately, then apply its
   lighting once the background column pass has released that queue. */
#define PENDING_LIGHT_EDITS 16
typedef struct
{
	int x,y,z;
	u8 b;
} PendingLightEdit;
static PendingLightEdit pendingLightEdits[PENDING_LIGHT_EDITS];
static int pendingLightEditCount;
static void apply_pending_light_edits(void);

#include "column_cache.inc"

/* ---------------- Modification ---------------- */

static void relight_edit(int x,int y,int z,u8 b)
{
	u32 i=widx(x,y,z);
	int ch,d;
	for(ch=0; ch<2; ++ch)
	{
		remove_light(ch,i);
		if(ch && g_blockDef[BLK_ID(b)].lightEmit)
		{
			lset(i,ch,g_blockDef[BLK_ID(b)].lightEmit);
			pq_push(i);
		}
		if(g_lightOpacity[BLK_ID(b)]<15)
		{
			for(d=0; d<6; ++d)
			{
				int nx=x+(d==1)-(d==0),ny=y+(d==3)-(d==2),nz=z+(d==5)-(d==4);
				if(in_world(nx,ny,nz))
				{
					u32 n=widx(nx,ny,nz);
					if((int)n<0) continue;
					if(lget(n,ch)) pq_push(n);
				}
				else if(0==ch && ny>=WH)
				{
					/* Open to the sky */
					lset(i,0,15);
					pq_push(i);
				}
			}
		}
		propagate(ch);
	}
}

static int pending_light_edit_can_queue(int x,int y,int z)
{
	int n;
	for(n=0; n<pendingLightEditCount; ++n)
	{
		if(pendingLightEdits[n].x==x && pendingLightEdits[n].y==y && pendingLightEdits[n].z==z)
		{
			return 1;
		}
	}
	return pendingLightEditCount<PENDING_LIGHT_EDITS;
}

static void pending_light_edit_queue(int x,int y,int z,u8 b)
{
	int n;
	for(n=0; n<pendingLightEditCount; ++n)
	{
		if(pendingLightEdits[n].x==x && pendingLightEdits[n].y==y && pendingLightEdits[n].z==z)
		{
			pendingLightEdits[n].b=b;
			return;
		}
	}
	if(n>=PENDING_LIGHT_EDITS) return;
	pendingLightEdits[n].x=x;
	pendingLightEdits[n].y=y;
	pendingLightEdits[n].z=z;
	pendingLightEdits[n].b=b;
	++pendingLightEditCount;
}

static void apply_pending_light_edits(void)
{
	int n=pendingLightEditCount;
	pendingLightEditCount=0;
	while(n-- > 0)
	{
		PendingLightEdit *e=&pendingLightEdits[n];
		if(in_world(e->x,e->y,e->z)) relight_edit(e->x,e->y,e->z,e->b);
	}
}

void world_set(int x,int y,int z,u8 b)
{
	u32 i;
	int deferLight;
	if(!in_world(x,y,z))
	{
		return;
	}
	i=widx(x,y,z);
	if((int)i<0) return;
	mesh_cancel();
	deferLight=cacheAllocated && cache_light_pending();
	if(deferLight && !pending_light_edit_can_queue(x,y,z))
	{
		/* The bounded queue is only a safety valve for a burst of edits. */
		cache_light_pump(0);
		deferLight=cache_light_pending();
	}
	g_blocks[i]=b;
	if(g_columnMap) residentDirty[i/COLUMN_CELLS]=1;
	update_height(x,z);
	if(deferLight) pending_light_edit_queue(x,y,z,b);
	else relight_edit(x,y,z,b);
	mark_dirty(x,y,z);
	mark_dirty(x-1,y,z);
	mark_dirty(x+1,y,z);
	mark_dirty(x,y-1,z);
	mark_dirty(x,y+1,z);
	mark_dirty(x,y,z-1);
	mark_dirty(x,y,z+1);
}

int world_is_solid(int x,int y,int z)
{
	return 0!=(blk_flags(wget(x,y,z))&BF_SOLID);
}

int world_surface_y(int x,int z)
{
	int y;
	for(y=WH-1; y>0; --y)
	{
		u8 b=wget(x,y-1,z);
		if(blk_flags(b)&BF_SOLID)
		{
			return y;
		}
		if(B_WATER==BLK_ID(b))
		{
			return y;
		}
	}
	return 1;
}

/* ---------------- Generation ---------------- */


/* Value noise.  Lattice values are hashed once per (octave, seed) into a
   small cache: the lattice for 256 blocks at spacing 8 is only 33x33. */
/* The supported finite extent is at most 256: spacing 8 needs 34 samples.
   Keep this generation scratch bounded while adding resident mob records. */
#define LAT_N 34
typedef struct
{
	u32 seed;
	int sh;
	u8 v[LAT_N*LAT_N];
} Lattice;
static Lattice lat[8];

static const u8 *lattice(int sh,u32 seed)
{
	int i,x,z;
	for(i=0; i<8; ++i)
	{
		if(lat[i].sh==sh && lat[i].seed==seed)
		{
			return lat[i].v;
		}
	}
	for(i=0; i<8 && lat[i].sh; ++i);
	if(i==8)
	{
		i=0;
	}
	lat[i].sh=sh;
	lat[i].seed=seed;
	for(z=0; z<LAT_N; ++z)
	{
		for(x=0; x<LAT_N; ++x)
		{
			lat[i].v[z*LAT_N+x]=hash3(x,z,0,seed)&255;
		}
	}
	return lat[i].v;
}

static int vnoise(int x,int z,int sh,u32 seed)
{
	int cx=x>>sh,cz=z>>sh;
	int fx=(x&((1<<sh)-1))<<(8-sh),fz=(z&((1<<sh)-1))<<(8-sh);
	int a,b,c,d,ab,cd;
	const u8 *L=lattice(sh,seed)+cz*LAT_N+cx;
	fx=(fx*fx*(768-2*fx))>>16;
	fz=(fz*fz*(768-2*fz))>>16;
	a=L[0];
	b=L[1];
	c=L[LAT_N];
	d=L[LAT_N+1];
	ab=a+(((b-a)*fx)>>8);
	cd=c+(((d-c)*fx)>>8);
	return ab+(((cd-ab)*fz)>>8);
}

static int terrain_height(int x,int z)
{
	int n=(vnoise(x,z,5,genSeed)*5+vnoise(x,z,4,genSeed+1)*3+vnoise(x,z,3,genSeed+2)*2)/10;
	int m=vnoise(x,z,6,genSeed+3);
	int h=SEA_LEVEL-2+(n-100)/7;
	int half=g_W/2,dx=x-half,dz=z-half;
	int d;
	if(m>150)
	{
		h+=(m-150)/5;
	}
	/* Island falloff: d is 0 at the center, 256 at the edge */
	d=(int)(isqrt((u32)(dx*dx+dz*dz)*65536u/(u32)(half*half)));
	if(d>150)
	{
		h-=(d-150)/8;
	}
	return CLAMP(h,4,WH-10);
}

static void set_raw(int x,int y,int z,u8 b)
{
	if(in_world(x,y,z))
	{
		g_blocks[widx(x,y,z)]=b;
	}
}

static void place_tree(int x,int y,int z)
{
	int h=4+hash3(x,z,5,genSeed)%3,i,dx,dy,dz;
	for(dy=h-2; dy<=h+1; ++dy)
	{
		int r=(dy>=h ? 1 : 2);
		for(dz=-r; dz<=r; ++dz)
		{
			for(dx=-r; dx<=r; ++dx)
			{
				if(ABS(dx)==r && ABS(dz)==r && (dy>=h || 0==hash3(x+dx,y+dy,z+dz,genSeed)%2))
				{
					continue;
				}
				if(B_AIR==wget(x+dx,y+dy,z+dz))
				{
					set_raw(x+dx,y+dy,z+dz,B_LEAVES);
				}
			}
		}
	}
	for(i=0; i<h; ++i)
	{
		set_raw(x,y+i,z,B_LOG);
	}
	set_raw(x,y-1,z,B_DIRT);
}

static void ore_cluster(u8 ore,int maxY,int size)
{
	int x=rnd_range(g_W),z=rnd_range(g_W),y=1+rnd_range(maxY),i;
	for(i=0; i<size; ++i)
	{
		if(in_world(x,y,z) && B_STONE==g_blocks[widx(x,y,z)])
		{
			g_blocks[widx(x,y,z)]=ore;
		}
		switch(rnd_range(6))
		{
		case 0: ++x; break;
		case 1: --x; break;
		case 2: ++y; break;
		case 3: --y; break;
		case 4: ++z; break;
		default:--z; break;
		}
	}
}

/* Caves and deep ores use their own random sequence, so the surface of a
   world is the same with or without them */
static u32 caveRng;
static int crnd(int n)
{
	caveRng=caveRng*1103515245u+12345u;
	return (int)((caveRng>>16)%(u32)n);
}

#define CAVE_TOP 14            /* Caves stay in the bottom chunk layer */

/* Lowest surface around a column: caves keep 5 blocks of rock below it */
static int min_height_around(int x,int z)
{
	int dx,dz,h=WH;
	for(dz=-1; dz<=1; ++dz)
	{
		for(dx=-1; dx<=1; ++dx)
		{
			int xx=CLAMP(x+dx,0,g_W-1),zz=CLAMP(z+dz,0,g_W-1);
			h=MIN(h,world_height(xx,zz));
		}
	}
	return h;
}

static void carve(int x,int y,int z)
{
	u32 i;
	u8 b;
	if(x<1 || z<1 || x>=g_W-1 || z>=g_W-1 || y<2 || y>CAVE_TOP)
	{
		return;
	}
	if(min_height_around(x,z)<y+5)
	{
		return;
	}
	i=widx(x,y,z);
	b=g_blocks[i];
	if(B_STONE==b || B_DIRT==b || B_GRAVEL==b || B_COAL_ORE==b || B_IRON_ORE==b)
	{
		g_blocks[i]=B_AIR;
	}
}

/* Worm caves: a point wanders with a slowly turning heading and carves a
   sphere of varying radius at every step (positions in 1/16 block) */
static void carve_caves(void)
{
	int w,n=g_W*g_W/350;
	for(w=0; w<n; ++w)
	{
		int px=crnd(g_W)*16,pz=crnd(g_W)*16,py=(3+crnd(CAVE_TOP-4))*16;
		int yaw=crnd(1024),len=40+crnd(90),r=20+crnd(12),s;
		for(s=0; s<len; ++s)
		{
			int x,y,z,rb=(r+15)/16;
			px+=(fsin(yaw)*16)>>14;
			pz+=(fcos(yaw)*16)>>14;
			py+=(crnd(3)-1)*5;
			py=CLAMP(py,3*16,(CAVE_TOP-1)*16);
			yaw+=crnd(81)-40;
			if(0==crnd(8))
			{
				r=CLAMP(r+crnd(13)-6,18,44);   /* Occasionally wider */
			}
			for(y=py/16-rb; y<=py/16+rb; ++y)
			{
				for(z=pz/16-rb; z<=pz/16+rb; ++z)
				{
					for(x=px/16-rb; x<=px/16+rb; ++x)
					{
						int dx=x*16+8-px,dy=y*16+8-py,dz=z*16+8-pz;
						if(dx*dx+dy*dy+dz*dz<=r*r)
						{
							carve(x,y,z);
						}
					}
				}
			}
		}
	}
}

/* Connect dry surface mouths to existing worm caves with walkable slopes.
   Ordinary worms stay sealed below oceans; only these checked paths may
   cut through the surface. */
/* Share validation/carving traversal to keep startup code small on 2 MB. */
static __attribute__((cold,noinline)) int entrance_tunnel(int x,int z,int h,int dx,int dz,int ey,int len,int carvePath)
{
	int s;
	for(s=0; s<=len; ++s)
	{
		int sx=x+dx*s,sz=z+dz*s,sy=MAX(ey,h-1-s);
		int xx,zz,yy;
		if(!carvePath && (min_height_around(sx,sz)<=SEA_LEVEL+1 ||
		   (ABS(sx-g_spawnX)<4 && ABS(sz-g_spawnZ)<4))) return 0;
		for(zz=sz-1; zz<=sz+1; ++zz)
		for(xx=sx-1; xx<=sx+1; ++xx)
		for(yy=sy; yy<sy+3; ++yy)
		{
			if(carvePath) set_raw(xx,yy,zz,B_AIR);
			else
			{
				u8 b=wget(xx,yy,zz);
				if(b==B_WATER || b==B_LOG || b==B_BEDROCK) return 0;
			}
		}
	}
	return 1;
}

static __attribute__((cold,noinline)) int carve_cave_entrances(void)
{
	int count=0,wanted=MAX(2,g_W*g_W/4000),tries=wanted*120;
	while(count<wanted && tries-->0)
	{
		int x=3+crnd(g_W-6),z=3+crnd(g_W-6),h,dir;
		if(count==0 && tries>wanted*60)
		{
			x=CLAMP(g_spawnX+crnd(49)-24,3,g_W-4);
			z=CLAMP(g_spawnZ+crnd(49)-24,3,g_W-4);
		}
		if(ABS(x-g_spawnX)<6 && ABS(z-g_spawnZ)<6) continue;
		h=world_height(x,z);
		if(h<=SEA_LEVEL+2 || B_GRASS!=wget(x,h-1,z)) continue;
		for(dir=0; dir<12; ++dir)
		{
			int heading=crnd(4),len=18+crnd(18);
			int dx=(heading==0)-(heading==1),dz=(heading==2)-(heading==3);
			int ex=x+dx*len,ez=z+dz*len,ey;
			if(ex<3 || ez<3 || ex>=g_W-3 || ez>=g_W-3) continue;
			for(ey=CAVE_TOP-2; ey>=3; --ey)
			{
				if(B_AIR==wget(ex,ey,ez) && B_AIR==wget(ex,ey+1,ez) &&
				   world_is_solid(ex,ey-1,ez)) break;
			}
			if(ey<3) continue;
			if(len<h-1-ey) continue;  /* At most one block down per step */
			/* Check the entire width before carving, including trees and
			   water, so failed candidates leave no partial surface holes. */
			if(!entrance_tunnel(x,z,h,dx,dz,ey,len,0)) continue;
			entrance_tunnel(x,z,h,dx,dz,ey,len,1);
			++count;
			break;
		}
	}
	return count;
}

static void deep_ore(u8 ore,int maxY,int size)
{
	int x=crnd(g_W),z=crnd(g_W),y=2+crnd(maxY-1),i;
	for(i=0; i<size; ++i)
	{
		if(in_world(x,y,z) && B_STONE==g_blocks[widx(x,y,z)])
		{
			g_blocks[widx(x,y,z)]=ore;
		}
		switch(crnd(6))
		{
		case 0: ++x; break;
		case 1: --x; break;
		case 2: ++y; break;
		case 3: --y; break;
		case 4: ++z; break;
		default:--z; break;
		}
	}
}

void world_generate(u32 seed)
{
	int x,z,y,i;
	if(cacheAllocated) generation_begin();
	genSeed=seed;
	genVersion=1;
	rnd_seed(seed);
	memset(lat,0,sizeof(lat));
	memset(g_blocks,0,(u32)g_W*g_W*WH);
	mesh_reset();
	trackLightDirty=0;
	for(z=0; z<g_W; ++z)
	{
		for(x=0; x<g_W; ++x)
		{
			int h=terrain_height(x,z);
			int beach=(h<=SEA_LEVEL+1);
			g_height[height_index(x,z)]=h;
			u32 base=widx(x,0,z);
			int gravelBed=(h<SEA_LEVEL-2 && vnoise(x,z,3,seed+9)>170);
			/* Fill the column in runs: bedrock, stone, dirt/sand, top, water */
			memset(g_blocks+base,B_AIR,WH);
			if(h-4>1)
			{
				memset(g_blocks+base+1,B_STONE,h-5);
			}
			for(y=MAX(1,h-4); y<h-1; ++y)
			{
				g_blocks[base+y]=beach ? B_SAND : B_DIRT;
			}
			g_blocks[base+h-1]=beach ? (gravelBed ? B_GRAVEL : B_SAND) : B_GRASS;
			for(y=h; y<SEA_LEVEL; ++y)
			{
				g_blocks[base+y]=B_WATER;
			}
			g_blocks[base]=B_BEDROCK;
			if(0==(rnd()&3)) g_blocks[base+1]=B_BEDROCK;
			if(0==(rnd()&7)) g_blocks[base+2]=B_BEDROCK;
		}
	}
	progress(25);
	/* Ores */
	for(i=0; i<g_W*g_W/48; ++i)
	{
		ore_cluster(B_COAL_ORE,SEA_LEVEL+8,6+rnd_range(6));
	}
	for(i=0; i<g_W*g_W/120; ++i)
	{
		ore_cluster(B_IRON_ORE,SEA_LEVEL-4,4+rnd_range(4));
	}
	/* Trees and plants */
	for(z=3; z<g_W-3; ++z)
	{
		for(x=3; x<g_W-3; ++x)
		{
			int h=world_height(x,z);
			u32 r=hash3(x,z,77,seed);
			int forest=vnoise(x,z,5,seed+5);
			if(B_GRASS!=wget(x,h-1,z))
			{
				continue;
			}
			if(r%1000<(u32)(forest>150 ? 45 : 8))
			{
				/* Keep trunks apart */
				int ok=1,dx,dz;
				for(dz=-2; dz<=2 && ok; ++dz)
				{
					for(dx=-2; dx<=2; ++dx)
					{
						if(B_LOG==wget(x+dx,h,z+dz) || B_LOG==wget(x+dx,h+1,z+dz))
						{
							ok=0;
							break;
						}
					}
				}
				if(ok && h+8<WH)
				{
					place_tree(x,h,z);
					continue;
				}
			}
			if(B_AIR==wget(x,h,z))
			{
				if(r%100<3)
				{
					set_raw(x,h,z,B_FLOWER);
				}
				else if(r%100<12)
				{
					set_raw(x,h,z,B_TALLGRASS);
				}
			}
		}
	}

	/* Spawn near the center on dry land */
	{
		int r;
		g_spawnX=g_W/2;
		g_spawnZ=g_W/2;
		for(r=0; r<g_W/2; ++r)
		{
			int found=0,k;
			for(k=0; k<8*r+1 && !found; ++k)
			{
				int sx=g_W/2+(k%3-1)*r+(k%7)-3,sz=g_W/2+((k/3)%3-1)*r+(k%5)-2;
				int sy=world_surface_y(sx,sz);
				if(in_world(sx,sy,sz) && B_GRASS==BLK_ID(wget(sx,sy-1,sz)))
				{
					g_spawnX=sx;
					g_spawnZ=sz;
					found=1;
				}
			}
			if(found)
			{
				break;
			}
		}
		g_spawnY=world_surface_y(g_spawnX,g_spawnZ);
	}

	/* Underground: caves, surface entrances, then deeper ores.  Pick the
	   spawn first so entrances can stay clear of its landing area. */
	caveRng=seed^0x5EEDCAFE;
	carve_caves();
	carve_cave_entrances();
	for(i=0; i<g_W*g_W/200; ++i)
	{
		deep_ore(B_IRON_ORE,CAVE_TOP,4+crnd(4));
	}
	for(i=0; i<g_W*g_W/320; ++i)
	{
		deep_ore(B_GOLD_ORE,12,3+crnd(4));
	}
	for(i=0; i<g_W*g_W/700; ++i)
	{
		deep_ore(B_DIAMOND_ORE,8,2+crnd(4));
	}
	progress(40);
	if(cacheAllocated) { if(!generation_finish()) fatal("Terrain column store full"); }
	else light_init();
	progress(60);

	/* Meshes are built by world_stream() around the player */
	progress(100);
	trackLightDirty=1;
}
