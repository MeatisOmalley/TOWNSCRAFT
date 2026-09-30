/* World storage, generation, lighting and chunk meshes. */
#include "world.h"
#include "sys.h"
#include "fmath.h"

int g_W,g_NC;
u8 *g_blocks,*g_light,*g_height;
int *g_zOff;
Chunk *g_chunks;
u32 *g_meshPool;
u32 g_meshQuads;
static u32 poolSize,poolTop;
static int strideZ;
int g_spawnX,g_spawnY,g_spawnZ;

const u8 g_lightOpacity[NUM_BLOCKS]=
{
	/* AIR STONE GRASS DIRT COBBLE PLANKS LOG LEAVES */
	0,15,15,15,15,15,15,1,
	/* SAND WATER GLASS BEDROCK COAL IRON CRAFT FURNACE */
	15,2,0,15,15,15,15,15,
	/* TORCH DOORL DOORU BEDF BEDH WOOL GRAVEL FLOWER */
	0,0,0,0,0,15,15,0,
	/* TALLGRASS STONEBRICK TNT */
	0,15,15,
};

/* ---------------- Allocation ---------------- */

void world_alloc(void)
{
	u32 highFree=heap_high_free();
	u32 cells;
	int z;
	/* Keep ~160KB of high memory for other large buffers. */
	u32 budget=(highFree>160*1024 ? highFree-160*1024 : 0);
	g_W=256;
	while(g_W>64 && (u32)g_W*g_W*WH*2>budget)
	{
		g_W-=16;
	}
	g_NC=g_W/CS;
	strideZ=g_W*WH;
	cells=(u32)g_W*g_W*WH;
	g_blocks=heap_alloc_high(cells);
	g_light=heap_alloc_high(cells);
	g_height=heap_alloc_low(g_W*g_W);
	g_zOff=heap_alloc_low(sizeof(int)*g_W);
	for(z=0; z<g_W; ++z)
	{
		g_zOff[z]=z*strideZ;
	}
	g_chunks=heap_alloc_low(sizeof(Chunk)*g_NC*g_NC*NCY);
	memset(g_chunks,0,sizeof(Chunk)*g_NC*g_NC*NCY);
	poolSize=(u32)g_W*g_W*5/4;   /* quads; a generated world uses about 0.7 per column */
	g_meshPool=heap_alloc_low(poolSize*8);
	poolTop=0;
}

/* ---------------- Light ---------------- */

#define PQ_LEN 16384
#define RQ_LEN 8192
static u32 *pq,*rq;
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

static inline void light_dirty(int x,int y,int z)
{
	int cx=x>>4,cy=y>>4,cz=z>>4,lx=x&15,ly=y&15,lz=z&15;
	Chunk *c=&g_chunks[chunk_index(cx,cy,cz)];
	if(!c->dirty) c->dirty=DIRTY_LIGHT;
	/* Faces lit by this cell may belong to the neighboring chunk */
	if(0==lx && cx>0) { c=&g_chunks[chunk_index(cx-1,cy,cz)]; if(!c->dirty) c->dirty=DIRTY_LIGHT; }
	if(15==lx && cx<g_NC-1) { c=&g_chunks[chunk_index(cx+1,cy,cz)]; if(!c->dirty) c->dirty=DIRTY_LIGHT; }
	if(0==ly && cy>0) { c=&g_chunks[chunk_index(cx,cy-1,cz)]; if(!c->dirty) c->dirty=DIRTY_LIGHT; }
	if(15==ly && cy<NCY-1) { c=&g_chunks[chunk_index(cx,cy+1,cz)]; if(!c->dirty) c->dirty=DIRTY_LIGHT; }
	if(0==lz && cz>0) { c=&g_chunks[chunk_index(cx,cy,cz-1)]; if(!c->dirty) c->dirty=DIRTY_LIGHT; }
	if(15==lz && cz<g_NC-1) { c=&g_chunks[chunk_index(cx,cy,cz+1)]; if(!c->dirty) c->dirty=DIRTY_LIGHT; }
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
	u32 zz=i/strideZ;
	u32 r=i-zz*strideZ;
	*z=zz;
	*x=r/WH;
	*y=r-(*x)*WH;
}

static void propagate(int ch)
{
	while(pqTail!=pqHead)
	{
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
					light_dirty(nx,ny,nz);
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
			nl=lget(n,ch);
			if(0==nl)
			{
				continue;
			}
			if(nl<L || (0==ch && 2==d && 15==L && 15==nl))
			{
				int emit=(ch ? g_blockDef[BLK_ID(g_blocks[n])].lightEmit : 0);
				lset(n,ch,0);
				light_dirty(nx,ny,nz);
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
	g_height[z*g_W+x]=y+1;
}

static void light_init(void)
{
	int x,z,y;
	/* Vertical sky pass */
	for(z=0; z<g_W; ++z)
	{
		for(x=0; x<g_W; ++x)
		{
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
			int h=g_height[z*g_W+x],mh=h;
			if(x>0) mh=MAX(mh,g_height[z*g_W+x-1]);
			if(x<g_W-1) mh=MAX(mh,g_height[z*g_W+x+1]);
			if(z>0) mh=MAX(mh,g_height[(z-1)*g_W+x]);
			if(z<g_W-1) mh=MAX(mh,g_height[(z+1)*g_W+x]);
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
	return g_light[widx(x,y,z)];
}

/* ---------------- Chunk meshes ---------------- */

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
static int nTmp;

static inline void emit(int lx,int ly,int lz,int dir,int w,int h,u32 w1,int model)
{
	if(nTmp<MAX_CHUNK_QUADS)
	{
		tmpQuads[nTmp*2]=lx|(lz<<4)|(ly<<8)|(dir<<12)|(model ? MQ_MODEL : 0)|((w-1)<<16)|((h-1)<<20);
		tmpQuads[nTmp*2+1]=w1;
		++nTmp;
	}
}

/* Greedy rectangles over a 16x16 key grid (index lz*16+lx).  Key 0 = no face. */
static void greedy2d(u16 *key,int ly,int dir)
{
	int lz,lx;
	for(lz=0; lz<16; ++lz)
	{
		for(lx=0; lx<16; ++lx)
		{
			u16 k=key[lz*16+lx];
			int w,h,i,ok;
			if(!k)
			{
				continue;
			}
			for(w=1; lx+w<16 && key[lz*16+lx+w]==k; ++w);
			for(h=1; lz+h<16; ++h)
			{
				ok=1;
				for(i=0; i<w; ++i)
				{
					if(key[(lz+h)*16+lx+i]!=k)
					{
						ok=0;
						break;
					}
				}
				if(!ok)
				{
					break;
				}
			}
			for(i=0; i<h; ++i)
			{
				memset(&key[(lz+i)*16+lx],0,w*2);
			}
			emit(lx,ly,lz,dir,w,h,(k-1)&0xFFFF,0);
		}
	}
}

/* Runs along z for each column lx (X-facing faces) */
static void runs1d_z(u16 *key,int ly,int dir)
{
	int lz,lx;
	for(lx=0; lx<16; ++lx)
	{
		for(lz=0; lz<16; ++lz)
		{
			u16 k=key[lz*16+lx];
			int h;
			if(!k)
			{
				continue;
			}
			for(h=1; lz+h<16 && key[(lz+h)*16+lx]==k; ++h)
			{
				key[(lz+h)*16+lx]=0;
			}
			emit(lx,ly,lz,dir,1,h,(k-1)&0xFFFF,0);
		}
	}
}

/* Runs along x for each row lz */
static void runs1d(u16 *key,int ly,int dir)
{
	int lz,lx;
	for(lz=0; lz<16; ++lz)
	{
		for(lx=0; lx<16; ++lx)
		{
			u16 k=key[lz*16+lx];
			int w;
			if(!k)
			{
				continue;
			}
			for(w=1; lx+w<16 && key[lz*16+lx+w]==k; ++w)
			{
				key[lz*16+lx+w]=0;
			}
			emit(lx,ly,lz,dir,w,1,(k-1)&0xFFFF,0);
		}
	}
}

static void pool_compact(void)
{
	int n=g_NC*g_NC*NCY,i,j;
	static u16 order[16*16*NCY];
	u32 w=0;
	for(i=0; i<n; ++i)
	{
		order[i]=i;
	}
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
	for(i=0; i<n; ++i)
	{
		Chunk *c=&g_chunks[order[i]];
		if(0==c->cap)
		{
			c->off=w;
			continue;
		}
		memmove(g_meshPool+w*2,g_meshPool+c->off*2,c->count*8);
		c->off=w;
		c->cap=c->count;
		w+=c->count;
	}
	poolTop=w;
}

static void rebuild_chunk(int cx,int cy,int cz)
{
	static u16 keyTop[256],keyBot[256],keyNZ[256],keyPZ[256],keyNX[256],keyPX[256];
	Chunk *c=&g_chunks[chunk_index(cx,cy,cz)];
	int n,lx,ly,lz;
	int x0=cx*CS,y0=cy*CS,z0=cz*CS;
	nTmp=0;
	for(ly=0; ly<CS; ++ly)
	{
		int y=y0+ly;
		memset(keyTop,0,sizeof(keyTop));
		memset(keyBot,0,sizeof(keyBot));
		memset(keyNZ,0,sizeof(keyNZ));
		memset(keyPZ,0,sizeof(keyPZ));
		memset(keyNX,0,sizeof(keyNX));
		memset(keyPX,0,sizeof(keyPX));
		for(lz=0; lz<CS; ++lz)
		{
			int z=z0+lz;
			for(lx=0; lx<CS; ++lx)
			{
				int x=x0+lx,d;
				u32 i=widx(x,y,z);
				u8 b=g_blocks[i],f;
				const BlockDef *def;
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
				for(d=0; d<6; ++d)
				{
					int nx=x+(d==1)-(d==0),ny=y+(d==3)-(d==2),nz=z+(d==5)-(d==4);
					u8 nb;
					u16 k;
					if(!in_world(nx,ny,nz))
					{
						if(ny<WH)
						{
							continue;   /* World edge and bottom: hidden */
						}
						nb=B_AIR;
					}
					else
					{
						nb=g_blocks[widx(nx,ny,nz)];
					}
					if(!face_visible(b,nb))
					{
						continue;
					}
					k=(u16)((def->tex[d]|(world_light_at(nx,ny,nz)<<8))+1);
					switch(d)
					{
					case DIR_NX: keyNX[lz*16+lx]=k; break;
					case DIR_PX: keyPX[lz*16+lx]=k; break;
					case DIR_NY: keyBot[lz*16+lx]=k; break;
					case DIR_PY: keyTop[lz*16+lx]=k; break;
					case DIR_NZ: keyNZ[lz*16+lx]=k; break;
					default:     keyPZ[lz*16+lx]=k; break;
					}
				}
			}
		}
		greedy2d(keyTop,ly,DIR_PY);
		greedy2d(keyBot,ly,DIR_NY);
		runs1d(keyNZ,ly,DIR_NZ);
		runs1d(keyPZ,ly,DIR_PZ);
		runs1d_z(keyNX,ly,DIR_NX);
		runs1d_z(keyPX,ly,DIR_PX);
	}
	/* Group by direction (models last); within a group order by plane
	   coordinate so visible planes come first: ascending for +X/+Y/+Z
	   (visible when the camera is above/after), descending for -X/-Y/-Z. */
	{
		static u16 cnt[7*16+1];
		static u8 bkt[MAX_CHUNK_QUADS];
		int i,g;
		memset(cnt,0,sizeof(cnt));
		for(i=0; i<nTmp; ++i)
		{
			u32 w0=tmpQuads[i*2];
			int k;
			if(w0&MQ_MODEL)
			{
				k=6*16;
			}
			else
			{
				int d=MQ_DIR(w0),p;
				p=(d<=DIR_PX) ? MQ_LX(w0) : (d<=DIR_PY ? MQ_LY(w0) : MQ_LZ(w0));
				if(0==(d&1))
				{
					p=15-p;
				}
				k=d*16+p;
			}
			bkt[i]=k;
			++cnt[k+1];
		}
		for(i=1; i<=7*16; ++i)
		{
			cnt[i]+=cnt[i-1];
		}
		for(g=0; g<7; ++g)
		{
			c->group[g]=(g<6 ? cnt[(g+1)*16] : nTmp)-cnt[g*16];
		}
		for(i=0; i<nTmp; ++i)
		{
			u16 d=cnt[bkt[i]]++;
			sortedQuads[d*2]=tmpQuads[i*2];
			sortedQuads[d*2+1]=tmpQuads[i*2+1];
		}
		n=nTmp;
	}
	if(n>c->cap)
	{
		u32 cap=n+16;
		if(poolTop+cap>poolSize)
		{
			c->cap=0;
			c->count=0;
			pool_compact();
		}
		if(poolTop+cap>poolSize)
		{
			cap=n;
		}
		if(poolTop+cap>poolSize)
		{
			/* Out of pool space: keep what fits */
			cap=(poolSize>poolTop ? poolSize-poolTop : 0);
			n=MIN((u32)n,cap);
		}
		c->off=poolTop;
		c->cap=cap;
		poolTop+=cap;
	}
	g_meshQuads+=n;
	g_meshQuads-=c->count;
	memcpy(g_meshPool+c->off*2,sortedQuads,n*8);
	c->count=n;
	c->dirty=0;
}

static void mark_dirty(int x,int y,int z)
{
	if(in_world(x,y,z))
	{
		g_chunks[chunk_index(x/CS,y/CS,z/CS)].dirty=DIRTY_GEOMETRY;
	}
}

void world_update_dirty_chunks(int maxLightOnly)
{
	int cx,cy,cz;
	for(cz=0; cz<g_NC; ++cz)
	{
		for(cx=0; cx<g_NC; ++cx)
		{
			for(cy=0; cy<NCY; ++cy)
			{
				u8 d=g_chunks[chunk_index(cx,cy,cz)].dirty;
				if(DIRTY_GEOMETRY==d || (DIRTY_LIGHT==d && maxLightOnly>0))
				{
					if(DIRTY_LIGHT==d)
					{
						--maxLightOnly;
					}
					rebuild_chunk(cx,cy,cz);
				}
			}
		}
	}
}

/* ---------------- Modification ---------------- */

void world_set(int x,int y,int z,u8 b)
{
	u32 i;
	int ch,d;
	if(!in_world(x,y,z))
	{
		return;
	}
	i=widx(x,y,z);
	g_blocks[i]=b;
	update_height(x,z);
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
					if(lget(n,ch))
					{
						pq_push(n);
					}
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

static u32 genSeed;

static int vnoise(int x,int z,int sh,u32 seed)
{
	int cx=x>>sh,cz=z>>sh;
	int fx=(x&((1<<sh)-1))<<(8-sh),fz=(z&((1<<sh)-1))<<(8-sh);
	int a,b,c,d,ab,cd;
	fx=(fx*fx*(768-2*fx))>>16;
	fz=(fz*fz*(768-2*fz))>>16;
	a=hash3(cx,cz,0,seed)&255;
	b=hash3(cx+1,cz,0,seed)&255;
	c=hash3(cx,cz+1,0,seed)&255;
	d=hash3(cx+1,cz+1,0,seed)&255;
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

void world_generate(u32 seed)
{
	int x,z,y,i;
	genSeed=seed;
	rnd_seed(seed);
	pq=heap_alloc_low(PQ_LEN*4);
	rq=heap_alloc_low(RQ_LEN*4);
	memset(g_blocks,0,(u32)g_W*g_W*WH);
	for(z=0; z<g_W; ++z)
	{
		for(x=0; x<g_W; ++x)
		{
			int h=terrain_height(x,z);
			int beach=(h<=SEA_LEVEL+1);
			u32 base=widx(x,0,z);
			int gravelBed=(h<SEA_LEVEL-2 && vnoise(x,z,3,seed+9)>170);
			for(y=0; y<WH; ++y)
			{
				u8 b=B_AIR;
				if(0==y || (y<=2 && 0==hash3(x,y,z,seed)%3))
				{
					b=B_BEDROCK;
				}
				else if(y<h-4)
				{
					b=B_STONE;
				}
				else if(y<h-1)
				{
					b=beach ? B_SAND : B_DIRT;
				}
				else if(y==h-1)
				{
					b=beach ? (gravelBed ? B_GRAVEL : B_SAND) : B_GRASS;
				}
				else if(y<SEA_LEVEL)
				{
					b=B_WATER;
				}
				g_blocks[base+y]=b;
			}
		}
	}
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
			int h=terrain_height(x,z);
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

	light_init();

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

	for(i=0; i<g_NC*g_NC*NCY; ++i)
	{
		g_chunks[i].dirty=DIRTY_GEOMETRY;
	}
	world_update_dirty_chunks(0);
	trackLightDirty=1;
}
