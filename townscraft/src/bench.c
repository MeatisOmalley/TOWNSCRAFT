/* Benchmark builds only (EXTRA="-DFIXED_SEED=4242 -DBENCH_EDIT").

   Measures, in emulated microseconds:
     - world generation time,
     - the frame hitch caused by block edits (place/remove near the player,
       on and off chunk borders),
     - frame times while moving across the world (mesh streaming).
   Results are in g_benchOut (see BO_*); g_benchOut[BO_DONE] becomes 1 at the
   end.  Read it from the emulator with MD. */
#include "common.h"
#include "sys.h"
#include "world.h"
#include "player.h"
#include "fmath.h"
#include "bench.h"

#ifdef BENCH_EDIT

u32 g_benchOut[BO_COUNT];

/* Microseconds from the 16-bit 1MHz free running counter (I/O 26h), with
   the 100Hz tick count resolving its wraparound */
static u32 usT,usLo,usAcc;
u32 bench_us(void)
{
	u32 t=g_ticks;
	u16 lo=inw(0x26);
	u32 dlo=(u16)(lo-(u16)usLo);
	int est=(int)(t-usT)*10000-(int)dlo;
	int k=(est>=0 ? (est+32768)>>16 : 0);
	usAcc+=dlo+((u32)k<<16);
	usT=t;
	usLo=lo;
	return usAcc;
}

static u32 genStart;
void bench_gen_begin(void)
{
	bench_us();
	genStart=bench_us();
}
void bench_gen_end(void)
{
	g_benchOut[BO_GEN_MS]=(bench_us()-genStart)/1000;
}

enum
{
	PH_SETTLE,PH_EDIT,PH_WALK,PH_DONE
};
/* Profile builds repeat the edit sequence for more samples */
#ifndef BENCH_REPS
#define BENCH_REPS 1
#endif
static int phase=PH_SETTLE,edit,editFrames;
static u32 phaseStart,lastFrame,updStart,frameUpd;
static int sx,sz,walkCX,walkCZ,walkR;
static int edX[BENCH_EDITS],edY[BENCH_EDITS],edZ[BENCH_EDITS];

/* Edit positions: near the player, on a chunk border, a chunk corner, and
   a two block deep hole */
static void plan_edits(void)
{
	static const s8 off[BENCH_EDITS/2][2]={{3,0},{0,3},{-3,2},{2,-3}};
	int i,bx=(sx|15),bz=(sz|15);
	for(i=0; i<4; ++i)
	{
		edX[i*2]=sx+off[i][0];
		edZ[i*2]=sz+off[i][1];
	}
	edX[8]=bx; edZ[8]=sz+2;          /* Border column (x=15 side) */
	edX[10]=bx+1; edZ[10]=sz-2;      /* Border column (x=0 side) */
	edX[12]=bx; edZ[12]=bz;          /* Chunk corner */
	edX[14]=sx-2; edZ[14]=sz-2;      /* Dig down (below) */
	for(i=0; i<BENCH_EDITS; i+=2)
	{
		edY[i]=world_surface_y(edX[i],edZ[i]);
		edX[i+1]=edX[i];
		edY[i+1]=edY[i];
		edZ[i+1]=edZ[i];
	}
	/* The last pair digs out the top block, then the one below it */
	edY[14]=edY[15]=edY[14]-1;
	edY[15]-=1;
}

static void do_edit(int i)
{
	if(i<14)
	{
		world_set(edX[i],edY[i],edZ[i],(i&1) ? B_AIR : B_STONE);
	}
	else
	{
		world_set(edX[i],edY[i],edZ[i],B_AIR);
	}
}

/* BENCH_PROF selects what the sampling profiler records: 1 world
   generation (the game's default), 2 world updates after edits, 3 world
   updates while walking, 4 whole frames after edits, 5 whole walk frames */
#ifndef BENCH_PROF
#define BENCH_PROF 1
#endif
extern int g_profEnable;
extern u32 g_profCount;
void bench_update_begin(void)
{
	updStart=bench_us();
	if((2==BENCH_PROF && PH_EDIT==phase) || (3==BENCH_PROF && PH_WALK==phase))
	{
		g_profEnable=1;
	}
}
void bench_update_end(void)
{
	frameUpd+=bench_us()-updStart;
	if(2==BENCH_PROF || 3==BENCH_PROF)
	{
		g_profEnable=0;
	}
}

static void phase_counts(int k)
{
	int i;
	for(i=0; i<4; ++i)
	{
		g_benchOut[BO_PHASE_COUNTS+k*4+i]=g_benchOut[BO_COMPACTS+i];
	}
}

static u32 frameCollect;
void bench_collect(u32 us)
{
	frameCollect+=us;
}

/* Called once per frame, before input and game ticks */
void bench_frame(void)
{
	u32 now=bench_us(),ft=now-lastFrame,upd=frameUpd,col=frameCollect;
	int first=(0==lastFrame);
	lastFrame=now;
	frameUpd=0;
	frameCollect=0;
	if(4==BENCH_PROF)
	{
		g_profEnable=(PH_EDIT==phase && editFrames<2);
	}
	if(5==BENCH_PROF)
	{
		g_profEnable=(PH_WALK==phase);
	}
	if(first)
	{
		phaseStart=now;
		sx=g_player.body.x>>12;
		sz=g_player.body.z>>12;
		plan_edits();
		return;
	}
	switch(phase)
	{
	case PH_SETTLE:
		g_player.yaw=0;
		g_player.pitch=-60;
		if(now-phaseStart>=2000000)
		{
			if(BENCH_PROF>1)
			{
				extern u32 g_profSamples[];
				g_profCount=0;
				memset(g_profSamples,0,4096*4);
			}
			phase=PH_EDIT;
			phase_counts(0);
			edit=-1;
			editFrames=99;
		}
		break;
	case PH_EDIT:
		/* The frame that just ended belongs to the current edit if it is
		   one of the 3 frames following it */
		if(edit>=0 && editFrames<3)
		{
			int e=edit%BENCH_EDITS;
			if(ft>g_benchOut[BO_EDIT+e])
			{
				g_benchOut[BO_EDIT+e]=ft;
			}
			if(upd>g_benchOut[BO_EDIT_UPD+e])
			{
				g_benchOut[BO_EDIT_UPD+e]=upd;
			}
			if(col>g_benchOut[BO_EDIT_COLLECT+e])
			{
				g_benchOut[BO_EDIT_COLLECT+e]=col;
			}
		}
		else if(edit>=0)
		{
			g_benchOut[BO_IDLE_SUM]+=ft;
			++g_benchOut[BO_IDLE_FRAMES];
		}
		++editFrames;
		if(editFrames>=8)
		{
			if(++edit>=BENCH_EDITS*BENCH_REPS)
			{
				phase=PH_WALK;
				phase_counts(1);
				phaseStart=now;
				walkCX=g_W/2;
				walkCZ=g_W/2;
				walkR=28;
				break;
			}
			do_edit(edit%BENCH_EDITS);
			editFrames=0;
		}
		break;
	case PH_WALK:
		{
			/* Circle of radius walkR at 5 blocks per second, facing ahead */
			u32 t=now-phaseStart;
			int a=(int)((t/1000u)*5u*1024u/(u32)(2*314*walkR/100)/1000u)&ANG_MASK;
			int cs=fcos(a),sn=fsin(a);
			int x=walkCX*FU+walkR*cs/4,z=walkCZ*FU+walkR*sn/4;
			if(t>BENCH_WALK_SEC*1000000u)
			{
				phase=PH_DONE;
				phase_counts(2);
				g_benchOut[BO_DONE]=1;
				break;
			}
			if(t>100000)
			{
				g_benchOut[BO_WALK_SUM]+=ft;
				++g_benchOut[BO_WALK_FRAMES];
				if(ft>g_benchOut[BO_WALK_MAX]) g_benchOut[BO_WALK_MAX]=ft;
				if(ft>66000) ++g_benchOut[BO_WALK_OVER66];
				if(ft>100000) ++g_benchOut[BO_WALK_OVER100];
				if(upd>g_benchOut[BO_WALK_UPD_MAX]) g_benchOut[BO_WALK_UPD_MAX]=upd;
			}
			g_player.body.x=x;
			g_player.body.z=z;
			g_player.body.y=world_surface_y(x>>12,z>>12)*FU;
			g_player.body.vx=g_player.body.vy=g_player.body.vz=0;
			g_player.body.fallDist=0;
			g_player.health=MAX_HEALTH;
			/* Moving counterclockwise: tangent direction */
			g_player.yaw=(-a)&ANG_MASK;
			g_player.pitch=-30;
		}
		break;
	default:
		g_player.health=MAX_HEALTH;
		break;
	}
}

#endif
