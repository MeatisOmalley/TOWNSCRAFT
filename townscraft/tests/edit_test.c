/* Native 32-bit regression test: meshes after partial (dirty layer)
   rebuilds must match full rebuilds.
   gcc -m32 -O2 -ffreestanding -fno-builtin -Isrc tests/edit_test.c
       src/blocks.c src/fmath.c src/libc.c build/tables.c -o build/edit_test
*/
#include "../src/world.c"

extern int printf(const char *,...);
extern void *calloc(unsigned int,unsigned int);
extern void free(void *);
extern void exit(int);
extern void qsort(void *,unsigned int,unsigned int,int (*)(const void *,const void *));

u32 heap_high_free(void) { return 1024*1024; }
u32 g_ramMB=4;
u32 heap_low_free(void) { return 200000; }
void *heap_alloc_low(u32 n) { return calloc(1,n); }
void *heap_alloc_high(u32 n) { return calloc(1,n); }
u32 render_mem_needed(int w) { (void)w; return 0; }

typedef struct { u32 w0,w1; } Q;
static int qcmp(const void *a,const void *b)
{
	const Q *x=a,*y=b;
	if(x->w0!=y->w0) return x->w0<y->w0 ? -1 : 1;
	if(x->w1!=y->w1) return x->w1<y->w1 ? -1 : 1;
	return 0;
}

/* Canonical copy of every chunk mesh: quads sorted within each group */
typedef struct
{
	u16 count,group[NGROUPS+1];
	u8 gbox[NGROUPS][3],sealed,meshed;
	Q *q;
} Snap;
static Snap *snap;

static void take(Snap *s)
{
	int i,n=g_NC*g_NC*NCY;
	for(i=0;i<n;++i)
	{
		Chunk *c=&g_chunks[i];
		int g,o=0;
		free(s[i].q);
		s[i].q=calloc(c->count+1,sizeof(Q));
		s[i].count=c->count;
		s[i].sealed=c->sealed;
		s[i].meshed=c->meshed;
		memcpy(s[i].group,c->group,sizeof(c->group));
		memcpy(s[i].gbox,c->gbox,sizeof(c->gbox));
		memcpy(s[i].q,g_meshPool+c->off*2,c->count*8);
		for(g=0;g<=NGROUPS;++g)
		{
			/* Within a group the order must ascend by sort key: check it,
			   then canonicalize */
			qsort(s[i].q+o,c->group[g],sizeof(Q),qcmp);
			o+=c->group[g];
		}
	}
}

static int check_order(void)
{
	int i,n=g_NC*g_NC*NCY;
	for(i=0;i<n;++i)
	{
		Chunk *c=&g_chunks[i];
		const u32 *q=g_meshPool+c->off*2;
		int g;
		for(g=0;g<NGROUPS;++g)
		{
			int d=g/NSUB,j,last=-1;
			for(j=0;j<c->group[g];++j,q+=2)
			{
				int lx=MQ_LX(q[0]),lz=MQ_LZ(q[0]),ly=MQ_LY(q[0]);
				int p=(d<=DIR_PX) ? lx : (d<=DIR_PY ? ly : lz);
				if(0==(d&1)) p=15-p;
				if(MQ_DIR(q[0])!=d || d*NSUB+(lx>=8)+((lz>=8)<<1)!=g || p<last)
				{
					printf("FAIL order chunk %d group %d\n",i,g);
					return 0;
				}
				last=p;
			}
		}
	}
	return 1;
}

static void compare(const Snap *part,const Snap *full,int step)
{
	int i,n=g_NC*g_NC*NCY;
	for(i=0;i<n;++i)
	{
		const Snap *a=&part[i],*b=&full[i];
		int g,j;
		if(!a->meshed) continue;
		if(a->count!=b->count || memcmp(a->group,b->group,sizeof(a->group)) ||
		   memcmp(a->q,b->q,a->count*sizeof(Q)) || a->sealed!=b->sealed)
		{
			printf("FAIL step %d chunk %d: count %d/%d sealed %d/%d\n",step,i,a->count,b->count,a->sealed,b->sealed);
			for(g=0;g<=NGROUPS;++g) if(a->group[g]!=b->group[g]) printf("  group %d: %d/%d box y %x\n",g,a->group[g],b->group[g],g<NGROUPS?a->gbox[g][2]:0);
			exit(1);
		}
		/* Partial boxes may be loose but must contain the exact ones */
		for(g=0;g<NGROUPS;++g)
		{
			if(0==b->group[g]) continue;
			for(j=0;j<3;++j)
			{
				if((a->gbox[g][j]&15)>(b->gbox[g][j]&15) || (a->gbox[g][j]>>4)<(b->gbox[g][j]>>4))
				{
					printf("FAIL step %d chunk %d group %d: box too small\n",step,i,g);
					exit(1);
				}
			}
		}
	}
}

static u32 rs=12345;
static int rn(int n) { rs=rs*1103515245u+12345u; return (rs>>16)%n; }

int main(void)
{
	static const u8 palette[]={B_AIR,B_AIR,B_AIR,B_STONE,B_GLASS,B_LEAVES,B_PLANKS,B_TORCH,B_FLOWER,B_DIRT};
	int width=96,step,edits=0;
	g_W=width; g_NC=width/CS; strideZ=width*WH;
	g_blocks=calloc(width*width*WH,1); g_light=calloc(width*width*WH,1);
	g_height=calloc(width*width,1); g_zOff=calloc(width,4);
	for(int z=0;z<width;++z) g_zOff[z]=z*strideZ;
	g_chunks=calloc(g_NC*g_NC*NCY,sizeof(Chunk));
	poolSize=200000; g_meshPool=calloc(poolSize,8);
	pq=calloc(PQ_LEN,4); rq=calloc(RQ_LEN,4); init_hides_tab();
	snap=calloc(g_NC*g_NC*NCY,sizeof(Snap));
	Snap *full=calloc(g_NC*g_NC*NCY,sizeof(Snap));
	world_generate(4242);
	world_stream(g_W/2,g_W/2,200,100000);
	for(step=0;step<300;++step)
	{
		int k,m=1+rn(12);
		for(k=0;k<m;++k)
		{
			/* Edits near the surface, around chunk borders and in caves */
			int x=rn(g_W),z=rn(g_W),y;
			if(rn(3)==0) x=(x|15)+rn(2)-0;
			if(rn(3)==0) z=(z|15)+rn(2)-0;
			x=CLAMP(x,0,g_W-1); z=CLAMP(z,0,g_W-1);
			y=rn(4)==0 ? rn(WH) : world_surface_y(x,z)-1+rn(3);
			if(!in_world(x,y,z)) continue;
			world_set(x,y,z,palette[rn(sizeof(palette))]);
			++edits;
			if(rn(4)==0)
			{
				/* Budgeted light: some chunks stay dirty across calls */
				world_update_dirty_chunks(rn(40));
			}
		}
		world_update_dirty_chunks(1000000);
		if(!check_order()) exit(1);
		take(snap);
		/* Full rebuild of everything for reference */
		for(int i=0;i<g_NC*g_NC*NCY;++i)
		{
			if(g_chunks[i].meshed)
				rebuild_chunk((i/NCY)%g_NC,i%NCY,i/(NCY*g_NC),0xFFFF);
		}
		take(full);
		compare(snap,full,step);
	}
	printf("PASS: %d edits, partial rebuilds match full rebuilds after 300 steps\n",edits);
	return 0;
}
