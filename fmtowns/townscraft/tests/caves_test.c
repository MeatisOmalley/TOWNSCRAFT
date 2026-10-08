/* Native 32-bit world generation and surface-to-cave access regressions. */
#include "../src/world.c"

extern int printf(const char *,...);
extern void *calloc(unsigned int,unsigned int);
extern void free(void *);
extern void exit(int);
u32 g_ramMB=2;
u32 heap_high_free(void) { return 1024*1024; }
u32 heap_low_free(void) { return 200000; }
void *heap_alloc_low(u32 n) { return calloc(1,n); }
void *heap_alloc_high(u32 n) { return calloc(1,n); }
u32 heap_high_mark(void) { return 0; }
void heap_high_rewind(u32 mark) {}
void fatal(const char *message) { printf("FATAL: %s\n",message); exit(1); }
u32 render_mem_needed(int w) { return 0; }
static void check(int condition,const char *message)
{
	if(!condition) { printf("FAIL W=%d seed=%u: %s\n",g_W,genSeed,message); exit(1); }
}
static int walkable(int x,int y,int z)
{
	return in_world(x,y+1,z) && y>0 && world_is_solid(x,y-1,z) &&
	       !world_is_solid(x,y,z) && !world_is_solid(x,y+1,z) &&
	       wget(x,y,z)!=B_WATER;
}
static int reachable_caves(void)
{
	u32 cells=g_W*g_W*WH,*queue=calloc(cells,4),head=0,tail=0;
	u8 *seen=calloc(cells,1);
	int found=0;
	u32 start=widx(g_spawnX,g_spawnY,g_spawnZ);
	check(walkable(g_spawnX,g_spawnY,g_spawnZ),"spawn remains safe");
	queue[tail++]=start; seen[start]=1;
	while(head<tail)
	{
		u32 i=queue[head++];
		int y=i%WH,x=(i/WH)%g_W,z=i/(g_W*WH);
		if(y<=CAVE_TOP-2 && terrain_height(x,z)-y>=5) ++found;
		for(int dir=0; dir<4; ++dir)
		{
			int nx=x+(dir==0)-(dir==1),nz=z+(dir==2)-(dir==3);
			for(int dy=1; dy>=-3; --dy)
			{
				int ny=y+dy;
				if(!walkable(nx,ny,nz)) continue;
				/* Clear headroom while stepping up or dropping down. */
				if(dy>0 && world_is_solid(x,y+2,z)) continue;
				int clear=1;
				for(int yy=ny; yy<=y+1; ++yy)
					if(world_is_solid(nx,yy,nz)) clear=0;
				if(!clear) continue;
				u32 ni=widx(nx,ny,nz);
				if(!seen[ni]) { seen[ni]=1; queue[tail++]=ni; }
				break;
			}
		}
	}
	free(seen); free(queue);
	return found;
}
int main(void)
{
	int widths[]={96,160,208,256};
	u32 seeds[]={1,42,4242,123456789};
	for(int w=0; w<ARRAY_LEN(widths); ++w)
	{
		g_W=widths[w]; g_NC=g_W/CS; strideZ=g_W*WH;
		g_ramMB=w==0 ? 2 : 4;
		g_blocks=calloc(g_W*g_W*WH,1); g_light=calloc(g_W*g_W*WH,1);
		g_height=calloc(g_W*g_W,1); g_zOff=calloc(g_W,4);
		for(int z=0; z<g_W; ++z) g_zOff[z]=z*strideZ;
		g_chunks=calloc(g_NC*g_NC*NCY,sizeof(Chunk));
		poolSize=mesh_pool_quads(); g_meshPool=calloc(poolSize,8);
		pq=calloc(PQ_LEN,4); rq=calloc(RQ_LEN,4); init_hides_tab();
		for(int seed=0; seed<ARRAY_LEN(seeds); ++seed)
		{
			world_generate(seeds[seed]);
			int reachable=reachable_caves();
			check(reachable>20,"walkable route from spawn reaches underground caves");
			for(int z=0; z<g_W; ++z)
			for(int x=0; x<g_W; ++x)
			{
				int h=terrain_height(x,z);
				check(wget(x,0,z)==B_BEDROCK,"bedrock floor is intact");
				if(h<SEA_LEVEL)
				{
					check(wget(x,h,z)==B_WATER,"ocean water is retained");
					check(world_is_solid(x,h-1,z),"ocean floor stays sealed");
				}
			}
			world_stream(g_spawnX,g_spawnZ,20,100000);
			check(g_meshQuads<=poolSize,"entrances stay within mesh budget");
			printf("PASS W=%d seed=%u: %d reachable cave floor cells\n",g_W,seeds[seed],reachable);
		}
		free(g_blocks); free(g_light); free(g_height); free(g_zOff);
		free(g_chunks); free(g_meshPool); free(pq); free(rq);
	}
	return 0;
}
