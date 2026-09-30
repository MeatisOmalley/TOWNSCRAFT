/* Common types and freestanding helpers. */
#ifndef COMMON_H
#define COMMON_H

#include "hw.h"

#define NULL ((void *)0)
#define ARRAY_LEN(a) (int)(sizeof(a)/sizeof((a)[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define CLAMP(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#define ABS(a) ((a)<0?-(a):(a))

void *memset(void *d,int c,unsigned int n);
void *memcpy(void *d,const void *s,unsigned int n);
void *memmove(void *d,const void *s,unsigned int n);
int strlen(const char *s);
char *itoa_dec(int v,char *buf);

/* Pseudo random */
u32 rnd(void);
void rnd_seed(u32 s);
int rnd_range(int n);   /* 0..n-1 */

/* Integer hash noise helpers (deterministic, world generation) */
u32 hash3(int x,int y,int z,u32 seed);

#endif
