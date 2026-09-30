#include "fmath.h"

extern const unsigned short g_atanTab[257];

int fatan2(int y,int x)
{
	int ax=ABS(x),ay=ABS(y),a;
	if(0==ax && 0==ay)
	{
		return 0;
	}
	if(ax>=ay)
	{
		a=g_atanTab[(ay<<8)/ax];
	}
	else
	{
		a=256-g_atanTab[(ax<<8)/ay];
	}
	if(x<0)
	{
		a=512-a;
	}
	if(y<0)
	{
		a=-a;
	}
	return a&ANG_MASK;
}

u32 isqrt(u32 v)
{
	u32 r=0,b=1u<<30;
	while(b>v)
	{
		b>>=2;
	}
	while(b)
	{
		if(v>=r+b)
		{
			v-=r+b;
			r=(r>>1)+b;
		}
		else
		{
			r>>=1;
		}
		b>>=2;
	}
	return r;
}
