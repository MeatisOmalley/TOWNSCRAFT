#include "physics.h"
#include "world.h"

static inline int fdiv(int v)
{
	return v>>12;   /* floor(v/4096) */
}

/* Does the box [x0,x1)x[y0,y1)x[z0,z1) (fine units) intersect a block box? */
int aabb_hits_world(int x0,int y0,int z0,int x1,int y1,int z1)
{
	int cx,cy,cz;
	for(cz=fdiv(z0); cz<=fdiv(z1-1); ++cz)
	{
		for(cx=fdiv(x0); cx<=fdiv(x1-1); ++cx)
		{
			for(cy=fdiv(y0); cy<=fdiv(y1-1); ++cy)
			{
				u8 lo[3],hi[3];
				u8 b=wget(cx,cy,cz);
				if(block_box(b,lo,hi))
				{
					int bx0=cx*FU+lo[0]*256,bx1=cx*FU+hi[0]*256;
					int by0=cy*FU+lo[1]*256,by1=cy*FU+hi[1]*256;
					int bz0=cz*FU+lo[2]*256,bz1=cz*FU+hi[2]*256;
					if(x0<bx1 && x1>bx0 && y0<by1 && y1>by0 && z0<bz1 && z1>bz0)
					{
						return 1;
					}
				}
			}
		}
	}
	return 0;
}

int body_touches_block(const Body *b,int bx,int by,int bz)
{
	int x0=b->x-b->hw,x1=b->x+b->hw,z0=b->z-b->hw,z1=b->z+b->hw,y0=b->y,y1=b->y+b->h;
	return x0<(bx+1)*FU && x1>bx*FU && y0<(by+1)*FU && y1>by*FU && z0<(bz+1)*FU && z1>bz*FU;
}

/* Move along one axis, stopping at the first block box in the way.  Only
   boxes that were not already overlapping are considered, so an entity
   that ends up inside a block can still walk out. */
static int move_axis(Body *b,int axis,int d)
{
	int lo[3],hi[3],cx,cy,cz,newPos,hit=0;
	lo[0]=b->x-b->hw; hi[0]=b->x+b->hw;
	lo[1]=b->y;       hi[1]=b->y+b->h;
	lo[2]=b->z-b->hw; hi[2]=b->z+b->hw;
	newPos=(&b->x)[axis]+d;
	{
		int slo[3],shi[3];
		int k;
		for(k=0; k<3; ++k)
		{
			slo[k]=lo[k];
			shi[k]=hi[k];
		}
		if(d>0) shi[axis]+=d; else slo[axis]+=d;
		for(cz=fdiv(slo[2]); cz<=fdiv(shi[2]-1); ++cz)
		{
			for(cx=fdiv(slo[0]); cx<=fdiv(shi[0]-1); ++cx)
			{
				for(cy=fdiv(slo[1]); cy<=fdiv(shi[1]-1); ++cy)
				{
					u8 bl[3],bh[3];
					int blo[3],bhi[3];
					u8 blk=wget(cx,cy,cz);
					if(!block_box(blk,bl,bh))
					{
						continue;
					}
					blo[0]=cx*FU+bl[0]*256; bhi[0]=cx*FU+bh[0]*256;
					blo[1]=cy*FU+bl[1]*256; bhi[1]=cy*FU+bh[1]*256;
					blo[2]=cz*FU+bl[2]*256; bhi[2]=cz*FU+bh[2]*256;
					/* Must overlap on the other two axes */
					for(k=0; k<3; ++k)
					{
						if(k!=axis && !(lo[k]<bhi[k] && hi[k]>blo[k]))
						{
							break;
						}
					}
					if(k<3)
					{
						continue;
					}
					if(d>0 && blo[axis]>=hi[axis])
					{
						int lim=blo[axis]-(hi[axis]-(&b->x)[axis]);
						if(lim<newPos)
						{
							newPos=lim;
							hit=1;
						}
					}
					else if(d<0 && bhi[axis]<=lo[axis])
					{
						int lim=bhi[axis]+((&b->x)[axis]-lo[axis]);
						if(lim>newPos)
						{
							newPos=lim;
							hit=1;
						}
					}
				}
			}
		}
	}
	(&b->x)[axis]=newPos;
	return hit;
}

void body_tick(Body *b,int gravity)
{
	int steps,i,vx,vy,vz,hitY=0,hitX=0,hitZ=0;
	int midY=(b->y+b->h/2)>>12;
	int headY=(b->y+b->h-600)>>12;
	b->inWater=(B_WATER==BLK_ID(wget(b->x>>12,midY,b->z>>12)) || B_WATER==BLK_ID(wget(b->x>>12,b->y>>12,b->z>>12)));
	b->headInWater=(B_WATER==BLK_ID(wget(b->x>>12,headY,b->z>>12)));
	if(gravity)
	{
		if(b->inWater)
		{
			b->vy-=gravity/5;
			b->vy=b->vy*7/8;
			if(b->vy<-1200) b->vy=-1200;
		}
		else
		{
			b->vy-=gravity;
			b->vy=b->vy*49/50;
			if(b->vy<-16000) b->vy=-16000;
		}
	}
	/* Sub-step so nothing moves more than half a block at a time */
	steps=1+(MAX(ABS(b->vx),MAX(ABS(b->vy),ABS(b->vz)))>>11);
	vx=b->vx/steps; vy=b->vy/steps; vz=b->vz/steps;
	for(i=0; i<steps; ++i)
	{
		if(vy) hitY|=move_axis(b,1,vy);
		if(vx) hitX|=move_axis(b,0,vx);
		if(vz) hitZ|=move_axis(b,2,vz);
	}
	b->onGround=0;
	if(hitY)
	{
		if(b->vy<0)
		{
			b->onGround=1;
		}
		b->vy=0;
	}
	else if(b->vy<=0)
	{
		/* Resting on something? Probe just below. */
		b->onGround=aabb_hits_world(b->x-b->hw,b->y-16,b->z-b->hw,b->x+b->hw,b->y,b->z+b->hw);
	}
	b->hitWall=hitX||hitZ;
	if(hitX) b->vx=0;
	if(hitZ) b->vz=0;
	if(!b->onGround && b->vy<0 && !b->inWater)
	{
		b->fallDist-=b->vy;
	}
}
