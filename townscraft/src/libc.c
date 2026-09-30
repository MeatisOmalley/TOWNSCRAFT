/* Minimal freestanding C library. */
#include "common.h"

void *memset(void *d,int c,unsigned int n)
{
	u8 *p=(u8 *)d;
	u32 v=(u8)c;
	v|=v<<8;
	v|=v<<16;
	while(n && ((u32)p&3))
	{
		*p++=(u8)c;
		--n;
	}
	{
		unsigned int nd=n>>2;
		__asm__ volatile("rep stosl":"+D"(p),"+c"(nd):"a"(v):"memory");
	}
	n&=3;
	while(n--)
	{
		*p++=(u8)c;
	}
	return d;
}

void *memcpy(void *d,const void *s,unsigned int n)
{
	void *dd=d;
	unsigned int nd=n>>2,nb=n&3;
	__asm__ volatile("rep movsl":"+D"(d),"+S"(s),"+c"(nd)::"memory");
	__asm__ volatile("rep movsb":"+D"(d),"+S"(s),"+c"(nb)::"memory");
	return dd;
}

void *memmove(void *d,const void *s,unsigned int n)
{
	u8 *dp=(u8 *)d;
	const u8 *sp=(const u8 *)s;
	if(dp<=sp || dp>=sp+n)
	{
		return memcpy(d,s,n);
	}
	while(n--)
	{
		dp[n]=sp[n];
	}
	return d;
}

int strlen(const char *s)
{
	int n=0;
	while(s[n])
	{
		++n;
	}
	return n;
}

char *itoa_dec(int v,char *buf)
{
	char tmp[12];
	int n=0,neg=0;
	char *p=buf;
	unsigned int u;
	if(v<0)
	{
		neg=1;
		u=(unsigned int)(-v);
	}
	else
	{
		u=(unsigned int)v;
	}
	do
	{
		tmp[n++]='0'+(u%10);
		u/=10;
	}while(u);
	if(neg)
	{
		*p++='-';
	}
	while(n)
	{
		*p++=tmp[--n];
	}
	*p=0;
	return buf;
}

static u32 rndState=0x12345678;

void rnd_seed(u32 s)
{
	rndState=s ? s : 1;
}

u32 rnd(void)
{
	u32 x=rndState;
	x^=x<<13;
	x^=x>>17;
	x^=x<<5;
	rndState=x;
	return x;
}

int rnd_range(int n)
{
	if(n<=0)
	{
		return 0;
	}
	return (int)((rnd()>>8)%(u32)n);
}

u32 hash3(int x,int y,int z,u32 seed)
{
	u32 h=seed;
	h^=(u32)x*0x27d4eb2d;
	h=(h^(h>>15))*0x85ebca6b;
	h^=(u32)y*0x165667b1;
	h=(h^(h>>13))*0xc2b2ae35;
	h^=(u32)z*0x9e3779b1;
	h=(h^(h>>16))*0x85ebca6b;
	h^=h>>13;
	return h;
}
