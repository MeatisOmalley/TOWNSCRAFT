/* Native 32-bit regression test.  Include the implementation to compare the
   bounded/cached search with the original exhaustive nearest-first search.
   gcc -m32 -O2 -ffreestanding -fno-builtin -Isrc tests/stream_test.c
       src/blocks.c src/fmath.c src/libc.c build/tables.c -o build/stream_test
*/
#include "../src/world.c"

extern int printf(const char *,...);
extern void *calloc(unsigned int,unsigned int);
extern void free(void *);
extern void exit(int);

u32 heap_high_free(void) { return 1024*1024; }
u32 g_ramMB=2;
u32 heap_low_free(void) { return 200000; }
void *heap_alloc_low(u32 n) { return calloc(1,n); }
void *heap_alloc_high(u32 n) { return calloc(1,n); }
u32 heap_high_mark(void) { return 0; }
void heap_high_rewind(u32 mark) {}
void fatal(const char *message) { printf("FATAL: %s\n",message); exit(1); }
u32 render_mem_needed(int w) { (void)w; return 0; }

static void reference_stream(int x,int z,int radius,int budget)
{
	streamX=x; streamZ=z;
	while(budget-->0)
	{
		int bx=-1,bz=0,by=0,bd=radius*radius+1;
		for(int cz=0;cz<g_NC;++cz)
		for(int cx=0;cx<g_NC;++cx)
		{
			int d=column_dist2(cx,cz,x,z);
			if(d<bd)
			for(int cy=0;cy<NCY;++cy)
			{
				if(!g_chunks[chunk_index(cx,cy,cz)].meshed)
				{
					bx=cx; bz=cz; by=cy; bd=d;
					break;
				}
			}
		}
		if(bx<0) return;
		build_chunk(bx,by,bz);
	}
}

static u32 digest(void)
{
	u32 h=2166136261u;
	for(int i=0;i<g_NC*g_NC*NCY;++i)
	{
		Chunk *c=&g_chunks[i];
		h=(h^c->meshed)*16777619u;
		h=(h^c->count)*16777619u;
		for(int j=0;j<c->count*2;++j)
			h=(h^g_meshPool[c->off*2+j])*16777619u;
	}
	return h;
}

static u32 expected[400];
static void scenario(int reference)
{
	void (*stream)(int,int,int,int)=reference ? reference_stream : world_stream;
	world_generate(4242);
	for(int i=0;i<400;++i)
	{
		/* Warm stationary frames, radius changes, movement, world edges,
		   eviction, and resetting at an unchanged camera position. */
		int x=i<80 ? g_spawnX : (i*7)%(g_W+8)-4;
		int z=i<80 ? g_spawnZ : (i*3)%(g_W+8)-4;
		int radius=i<40 ? 16 : 20;
		if(i==20 || i==300) mesh_reset();
		if(i==30) unmesh_column(g_spawnX/CS,g_spawnZ/CS);
		stream(x,z,radius,i==0 || i==20 ? 100000 : (i%5));
		u32 h=digest();
		if(reference) expected[i]=h;
		else if(h!=expected[i])
		{
			printf("FAIL width=%d step=%d expected=%u actual=%u\n",g_W,i,expected[i],h);
			exit(1);
		}
	}
}

int main(void)
{
	for(g_ramMB=2;g_ramMB<=4;g_ramMB+=2)
	for(int width=80;width<=256;width+=(width==80 ? 16 : 80))
	{
		g_W=width; g_NC=width/CS; strideZ=width*WH;
		g_blocks=calloc(width*width*WH,1); g_light=calloc(width*width*WH,1);
		g_height=calloc(width*width,1); g_zOff=calloc(width,4);
		for(int z=0;z<width;++z) g_zOff[z]=z*strideZ;
		g_chunks=calloc(g_NC*g_NC*NCY,sizeof(Chunk));
		poolSize=mesh_pool_quads(); g_meshPool=calloc(poolSize,8);
		pq=calloc(PQ_LEN,4); rq=calloc(RQ_LEN,4); init_hides_tab();
		scenario(1); scenario(0);
		printf("PASS RAM=%u width=%d: 400 streaming states match exhaustive search\n",g_ramMB,width);
		free(g_blocks); free(g_light); free(g_height); free(g_zOff);
		free(g_chunks); free(g_meshPool); free(pq); free(rq);
	}
	return 0;
}
