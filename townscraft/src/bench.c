/* Micro benchmarks (debug only).  Results in CPU cycles per operation. */
#include "common.h"
#include "sys.h"
#include "gfx.h"
#include "fmath.h"
#include "textures.h"

void span_opaque2(u8 *dst,int count,const u8 *tile,u32 u,u32 v,int du,int dv);
void span_opaque1(u8 *dst,int count,const u8 *tile,u32 u,u32 v,int du,int dv);
extern u32 g_benchFreqMHz;

static int bench_line;
static void report(const char *name,u32 ticks,u32 ops)
{
	char buf[32];
	/* cycles/op = ticks*10ms*freq / ops */
	u32 cyc=(ticks*10000u/ops)*g_benchFreqMHz/1000u;   /* in 1/1000 cycle? keep simple */
	gfx_text_shadow(g_fb,4,40+bench_line*10,name,C_WHITE,C_BLACK);
	gfx_text_shadow(g_fb,160,40+bench_line*10,itoa_dec(cyc,buf),C_YELLOW,C_BLACK);
	++bench_line;
}

volatile int sink;

void run_bench(void)
{
	u32 t,i;
	static u8 dst[640];
	const u8 *tile=TEX_TILE(0,15);
	bench_line=0;

	t=g_ticks; while(t==g_ticks);
	t=g_ticks;
	for(i=0; i<20000; ++i)
	{
		span_opaque2(dst,100,tile,0x10000,0x10000,0x8000,0x3000);
	}
	report("span2 x1000/px",g_ticks-t,2000);

	t=g_ticks; while(t==g_ticks);
	t=g_ticks;
	for(i=0; i<20000; ++i)
	{
		span_opaque1(dst,100,tile,0x10000,0x10000,0x8000,0x3000);
	}
	report("span1 x1000/px",g_ticks-t,2000);

	t=g_ticks; while(t==g_ticks);
	t=g_ticks;
	for(i=0; i<200000; ++i)
	{
		sink=mulshift(i,sink+12345,16);
	}
	report("mulshift x1000",g_ticks-t,200);

	t=g_ticks; while(t==g_ticks);
	t=g_ticks;
	for(i=0; i<200000; ++i)
	{
		sink=divshift(i,sink+12345,4);
	}
	report("divshift x1000",g_ticks-t,200);

	t=g_ticks; while(t==g_ticks);
	t=g_ticks;
	for(i=0; i<200000; ++i)
	{
		sink+=i;
	}
	report("loop x1000",g_ticks-t,200);

	t=g_ticks; while(t==g_ticks);
	t=g_ticks;
	for(i=0; i<2000; ++i)
	{
		memcpy(g_fb+320*100,g_fb,320*100);
	}
	report("memcpy x1000/B",g_ticks-t,64000);
}
