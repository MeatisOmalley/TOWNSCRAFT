/* Fixed-point math.  Angles are 0..1023 per full turn.  Trig values are
   2.14 fixed point (16384 = 1.0). */
#ifndef FMATH_H
#define FMATH_H
#include "common.h"

#define ANG_MASK 1023
#define FIX14 16384

extern const short g_sinTab[1024];
static inline int fsin(int a){return g_sinTab[a&ANG_MASK];}
static inline int fcos(int a){return g_sinTab[(a+256)&ANG_MASK];}
int fatan2(int y,int x);     /* returns angle 0..1023 */
u32 isqrt(u32 v);

/* (a*b)>>s with a 64-bit intermediate */
static inline int mulshift(int a,int b,int s)
{
	int lo,hi;
	if(__builtin_constant_p(s))
	{
		__asm__("imull %3\n\tshrdl %4,%%edx,%%eax":"=a"(lo),"=d"(hi):"a"(a),"rm"(b),"I"(s):"cc");
		return lo;
	}
	__asm__("imull %3":"=a"(lo),"=d"(hi):"a"(a),"rm"(b));
	return (int)(((u32)lo>>s)|((u32)hi<<(32-s)));
}
/* (a<<s)/b with a 64-bit intermediate.  Caller guarantees no overflow. */
static inline int divshift(int a,int b,int s)
{
	int q,r;
	int lo=(int)((u32)a<<s),hi=a>>(32-s);
	__asm__("idivl %4":"=a"(q),"=d"(r):"a"(lo),"d"(hi),"rm"(b));
	return q;
}

/* (a*b)/c with a 64-bit intermediate */
static inline int muldiv(int a,int b,int c)
{
	int q,r;
	__asm__("imull %2\n\tidivl %3":"=a"(q),"=&d"(r):"r"(b),"r"(c),"a"(a));
	return q;
}

#endif
