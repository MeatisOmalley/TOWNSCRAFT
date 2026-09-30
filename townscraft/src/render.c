/* Voxel renderer.

   Painter's algorithm without a depth buffer.  Chunk meshes hold greedy-
   merged quads (see world.c).  Primitives are drawn in a nested back-to-front
   order: y slices far to near, rows (z) far to near within a slice, cells (x)
   far to near within a row.  A cell that occludes another lies between it
   and the camera on every axis, so this lexicographic order is correct for
   cells.  A merged horizontal quad is drawn at the end of its slice and a
   merged Z-facing run at the end of its row: everything it can hide lies
   behind its plane and was drawn earlier, and everything that can hide it
   lies in front and comes later.  The order is produced with a radix sort.

   Camera-space positions of grid corners are sums of per-axis tables
   (TX[x]+TY[y]+TZ[z]), so transforming a face costs additions only. */
#include "render.h"
#include "raster.h"
#include "fmath.h"
#include "world.h"
#include "textures.h"
#include "sys.h"
#include "gfx.h"

Camera g_cam;
int g_viewDist=16;
int g_renderScale=2;
u32 g_statFaces,g_statItems;
u32 g_prof[8];
u32 g_dbg[4];
int g_flatLOD=6*16;   /* 28.4: faces smaller than this are flat shaded */
int g_flatDist;       /* Blocks: farther faces are flat shaded (0 = off) */

#define NEAR_Z 12       /* units (~0.05 block) */
#define MAX_BOXES 160
#define GUARD 48        /* pixels outside the viewport before 2D clipping */
#define INV_NEAR 4096   /* Projection reciprocal table: see project() */
#define INV_FAR  (INV_NEAR+4096*8)

typedef struct { int c[3]; } V3;

static V3 *TX,*TY,*TZ;           /* Camera-space of grid lines, per axis */
static V3 AX16[17],AY16[17],AZ16[17];  /* Axis step of k/16 block */
static u8 *visX,*visY,*visZ;
static u32 *invTab;               /* See project() */
/* Frustum planes as linear functions of camera space, tabulated per grid
   line like TX/TY/TZ: plane k of a point is PX[k][x]+PY[k][y]+PZ[k][z].
   A sphere of radius r is outside plane k when that exceeds r*planeLen[k].
   Planes: behind the camera, right, left, top, bottom. */
#define NPLANES 5
static int *PX[NPLANES],*PY[NPLANES],*PZ[NPLANES];
static int planeLen[NPLANES];
static int rot[3][3];            /* Rows: right, up, forward (2.14) */
static int camBX,camBY,camBZ;
static int vw,vh,focal16,cx16,cy16;
static int skyDarkenCur;
static u8 texTransparent[NUM_TEXTURES];
static u8 texAvg[NUM_TEXTURES];

/* Draw list.
   a: x | z<<8 | y<<16 | dir<<22 | kind<<25
   b: quad: tex | light<<8 | (w-1)<<16 | (h-1)<<20;  model: block;  box: index */
enum
{
	IK_QUAD,IK_MODEL,IK_BOX
};
typedef struct
{
	u32 a,b;
} Item;
static Item *items;
static u32 *itemKey;
static u16 *order,*order2;
static int maxItems,nItems,nStatic;
static u32 entKey[MAX_BOXES];
static u16 entBox[MAX_BOXES];
static int nEnt;

static MBox boxes[MAX_BOXES];
static int sqTab[256];
static int nBoxes;

static u8 dirShade[6]={2,2,3,0,1,1};
/* Corner selection per face: which of lo/hi on each axis, TL,TR,BR,BL */
static const u8 faceCorner[6][4][3]=
{
	{{0,1,1},{0,1,0},{0,0,0},{0,0,1}},  /* -X */
	{{1,1,0},{1,1,1},{1,0,1},{1,0,0}},  /* +X */
	{{0,0,1},{1,0,1},{1,0,0},{0,0,0}},  /* -Y */
	{{0,1,0},{1,1,0},{1,1,1},{0,1,1}},  /* +Y */
	{{0,1,0},{1,1,0},{1,0,0},{0,0,0}},  /* -Z */
	{{1,1,1},{0,1,1},{0,0,1},{1,0,1}},  /* +Z */
};

typedef struct
{
	int x,y,z,u,v;
} CVert;

static int max_items_for_ram(void)
{
	return g_ramMB>=4 ? 8000 : 3000;
}

u32 render_mem_needed(int w)
{
	return (u32)max_items_for_ram()*(sizeof(Item)+4+2+2)
	      +sizeof(V3)*(2*(w+2)+WH+2)+2*(w+2)+WH+2
	      +sizeof(int)*NPLANES*(2*(w+2)+WH+2)
	      +sizeof(u32)*(INV_NEAR+4096);
}

void render_init(void)
{
	int t,i;
	TX=heap_alloc_low(sizeof(V3)*(g_W+2));
	TZ=heap_alloc_low(sizeof(V3)*(g_W+2));
	TY=heap_alloc_low(sizeof(V3)*(WH+2));
	visX=heap_alloc_low(g_W+2);
	visZ=heap_alloc_low(g_W+2);
	visY=heap_alloc_low(WH+2);
	for(i=0; i<NPLANES; ++i)
	{
		PX[i]=heap_alloc_low(sizeof(int)*(g_W+2));
		PZ[i]=heap_alloc_low(sizeof(int)*(g_W+2));
		PY[i]=heap_alloc_low(sizeof(int)*(WH+2));
	}
	invTab=heap_alloc_low(sizeof(u32)*(INV_NEAR+4096));
	maxItems=max_items_for_ram();
	for(i=0; i<256; ++i)
	{
		sqTab[i]=i*i;
	}
	items=heap_alloc_low(sizeof(Item)*maxItems);
	itemKey=heap_alloc_low(4*maxItems);
	order=heap_alloc_low(2*maxItems);
	order2=heap_alloc_low(2*maxItems);
	for(t=0; t<NUM_TEXTURES; ++t)
	{
		texTransparent[t]=0;
		for(i=0; i<256; ++i)
		{
			if(0==g_tex[t][i])
			{
				texTransparent[t]=1;
				break;
			}
		}
		texAvg[t]=texture_average(t);
	}
}

void render_clear_boxes(void)
{
	nBoxes=0;
}

MBox *render_add_box(void)
{
	if(nBoxes<MAX_BOXES)
	{
		MBox *b=&boxes[nBoxes++];
		memset(b,0,sizeof(*b));
		return b;
	}
	return NULL;
}

int face_light_level(int lightByte,int dir,int skyDarken)
{
	int sky=(lightByte>>4)-skyDarken,blk=lightByte&15,L;
	L=MAX(sky,blk);
	L-=dirShade[dir];
	return CLAMP(L,0,15);
}

/* ---------- Projection and clipping ---------- */

static int clip_near(const CVert *in,int n,CVert *out)
{
	int i,m=0;
	for(i=0; i<n; ++i)
	{
		const CVert *a=&in[i],*b=&in[(i+1)%n];
		int ain=(a->z>=NEAR_Z),bin=(b->z>=NEAR_Z);
		if(ain)
		{
			out[m++]=*a;
		}
		if(ain!=bin)
		{
			int t=divshift(NEAR_Z-a->z,b->z-a->z,16);
			CVert *o=&out[m++];
			o->x=a->x+mulshift(b->x-a->x,t,16);
			o->y=a->y+mulshift(b->y-a->y,t,16);
			o->z=NEAR_Z;
			o->u=a->u+mulshift(b->u-a->u,t,16);
			o->v=a->v+mulshift(b->v-a->v,t,16);
		}
	}
	return m;
}

/* Clip polygon against one screen edge.  axis 0=x 1=y, sign +1 keep >=bound, -1 keep <=bound */
static int clip_2d(const RVert *in,int n,RVert *out,int axis,int bound,int sign)
{
	int i,m=0;
	for(i=0; i<n; ++i)
	{
		const RVert *a=&in[i],*b=&in[(i+1)%n];
		int av=(axis ? a->y : a->x),bv=(axis ? b->y : b->x);
		int ain=(sign>0 ? av>=bound : av<=bound),bin=(sign>0 ? bv>=bound : bv<=bound);
		if(ain)
		{
			out[m++]=*a;
		}
		if(ain!=bin)
		{
			int t=divshift(bound-av,bv-av,16);
			RVert *o=&out[m++];
			o->x=a->x+mulshift(b->x-a->x,t,16);
			o->y=a->y+mulshift(b->y-a->y,t,16);
			o->u=a->u+mulshift(b->u-a->u,t,16);
			o->v=a->v+mulshift(b->v-a->v,t,16);
			if(axis)
			{
				o->y=bound;
			}
			else
			{
				o->x=bound;
			}
		}
	}
	return m;
}

/* (focal16<<16)/z without a divide: exact for z < INV_NEAR, then in steps
   of 8 up to INV_FAR (0.2% at most, and the same for every face sharing a
   corner, so no cracks) */
static int invFocal;

static void build_inv_table(void)
{
	int i;
	for(i=1; i<INV_NEAR; ++i)
	{
		invTab[i]=((u32)focal16<<16)/(u32)i;
	}
	for(i=0; i<4096; ++i)
	{
		invTab[INV_NEAR+i]=((u32)focal16<<16)/(u32)(INV_NEAR+i*8+4);
	}
	invFocal=focal16;
}

static inline void project(int x,int y,int z,int *sx,int *sy)
{
	int inv=(z<INV_NEAR) ? (int)invTab[z] : (z<INV_FAR ? (int)invTab[INV_NEAR+((z-INV_NEAR)>>3)] : (focal16<<16)/z);
	*sx=cx16+mulshift(x,inv,16);
	*sy=cy16-mulshift(y,inv,16);
}

/* Draw a projected polygon: off-screen and tiny rejection, guard band clip.
   flags: RP_TRANSPARENT, RP_WRAP. */
static void draw_spoly(RVert *sv,int n,const u8 *tex,int flags,u8 flat)
{
	RVert tmp[12];
	int i,m,minx=0x7FFFFFFF,maxx=-0x7FFFFFFF,miny=0x7FFFFFFF,maxy=-0x7FFFFFFF;
	for(i=0; i<n; ++i)
	{
		if(sv[i].x<minx) minx=sv[i].x;
		if(sv[i].x>maxx) maxx=sv[i].x;
		if(sv[i].y<miny) miny=sv[i].y;
		if(sv[i].y>maxy) maxy=sv[i].y;
	}
	if(maxx<0 || maxy<0 || minx>=vw*16 || miny>=vh*16)
	{
		return;
	}
	++g_statFaces;
	/* Tiny face: one pixel */
	if(maxx-minx<20 && maxy-miny<20)
	{
		if(!(flags&RP_TRANSPARENT))
		{
			raster_pixel((minx+maxx)>>5,(miny+maxy)>>5,flat);
		}
		return;
	}
	/* Small face: flat shaded with the texture's average color */
	if(!(flags&RP_TRANSPARENT) && maxx-minx<g_flatLOD && maxy-miny<g_flatLOD)
	{
		tex=NULL;
	}
	m=n;
	if(minx<-GUARD*16 || maxx>(vw+GUARD)*16 || miny<-GUARD*16 || maxy>(vh+GUARD)*16)
	{
		m=clip_2d(sv,m,tmp,0,-GUARD*16,1);
		m=clip_2d(tmp,m,sv,0,(vw+GUARD)*16,-1);
		m=clip_2d(sv,m,tmp,1,-GUARD*16,1);
		m=clip_2d(tmp,m,sv,1,(vh+GUARD)*16,-1);
		if(m<3)
		{
			return;
		}
	}
	if(tex)
	{
		raster_poly(sv,m,tex,flags);
	}
	else
	{
		raster_flat_poly(sv,m,flat);
	}
}

/* Draw a camera-space polygon (n<=4 input vertices). */
static void draw_cpoly(const CVert *cv,int n,const u8 *tex,int flags,u8 flat)
{
	CVert clipped[8];
	RVert sv[12];
	int i;
	for(i=0; i<n; ++i)
	{
		if(cv[i].z<NEAR_Z)
		{
			n=clip_near(cv,n,clipped);
			if(n<3)
			{
				return;
			}
			cv=clipped;
			break;
		}
	}
	for(i=0; i<n; ++i)
	{
		project(cv[i].x,cv[i].y,cv[i].z,&sv[i].x,&sv[i].y);
		sv[i].u=cv[i].u;
		sv[i].v=cv[i].v;
	}
	draw_spoly(sv,n,tex,flags,flat);
}

static inline int to_screen(const V3 *p,int *sx,int *sy)
{
	if(p->c[2]<NEAR_Z)
	{
		return 0;
	}
	project(p->c[0],p->c[1],p->c[2],sx,sy);
	return 1;
}

/* ---------- Faces and models ---------- */

#define UVMAX ((16<<16)-0x200)
#define UVMIN 0x200

static inline int uvc(int k)
{
	/* k in 1/16 block -> 16.16 texel, kept inside the texture */
	int v=k<<16;
	return CLAMP(v,UVMIN,UVMAX);
}

/* Axis-aligned box inside cell (bx,by,bz).  lo/hi in 1/16.  uvRot rotates
   the top/bottom texture by 90 degree steps. */
static void draw_box_face(int bx,int by,int bz,const u8 *lo,const u8 *hi,int dir,int texId,int level,int uvRot)
{
	CVert cv[4];
	int k;
	for(k=0; k<4; ++k)
	{
		const u8 *sel=faceCorner[dir][k];
		int x=sel[0] ? hi[0] : lo[0];
		int y=sel[1] ? hi[1] : lo[1];
		int z=sel[2] ? hi[2] : lo[2];
		int u,v,c;
		/* Index the grid tables by the integer part so that corners shared
		   with neighboring cells come out bit-identical (no cracks). */
		const V3 *tx=&TX[bx+(x>>4)],*ty=&TY[by+(y>>4)],*tz=&TZ[bz+(z>>4)];
		for(c=0; c<3; ++c)
		{
			(&cv[k].x)[c]=tx->c[c]+ty->c[c]+tz->c[c]+AX16[x&15].c[c]+AY16[y&15].c[c]+AZ16[z&15].c[c];
		}
		switch(dir)
		{
		case DIR_NX: u=16-z; v=16-y; break;
		case DIR_PX: u=z;    v=16-y; break;
		case DIR_NZ: u=x;    v=16-y; break;
		case DIR_PZ: u=16-x; v=16-y; break;
		case DIR_PY: u=x;    v=z;    break;
		default:     u=x;    v=16-z; break;
		}
		switch(uvRot&3)
		{
		case 1: c=u; u=16-v; v=c; break;
		case 2: u=16-u; v=16-v; break;
		case 3: c=u; u=v; v=16-c; break;
		}
		cv[k].u=uvc(u);
		cv[k].v=uvc(v);
	}
	draw_cpoly(cv,4,TEX_TILE(texId,level),texTransparent[texId],g_shadeLUT[level][texAvg[texId]]);
}

/* Face of a sub-box is visible if the camera is on the outer side of its plane */
static inline int box_face_visible(int bx,int by,int bz,const u8 *lo,const u8 *hi,int dir)
{
	switch(dir)
	{
	case DIR_NX: return g_cam.x<bx*256+lo[0]*16;
	case DIR_PX: return g_cam.x>bx*256+hi[0]*16;
	case DIR_NY: return g_cam.y<by*256+lo[1]*16;
	case DIR_PY: return g_cam.y>by*256+hi[1]*16;
	case DIR_NZ: return g_cam.z<bz*256+lo[2]*16;
	default:     return g_cam.z>bz*256+hi[2]*16;
	}
}

static int light_level_at(int x,int y,int z,int dir)
{
	return face_light_level(world_light_at(x,y,z),dir,skyDarkenCur);
}

/* A mesh quad: extends w cells along x and h cells along z (top/bottom),
   or w cells along x (Z-facing), and repeats the texture per cell. */
static void draw_quad(int x,int y,int z,int dir,int w,int h,int tex,int light)
{
	int ex=(dir<=DIR_PX) ? 1 : w;
	int ez=(dir<=DIR_PY) ? h : 1;
	/* Near the camera, affine texturing of a big quad visibly warps: draw it
	   cell by cell instead.  Painter's order is unaffected (same plane). */
	if((ex>1 || ez>1) && ABS(x+ex/2-camBX)<=ex/2+2 && ABS(z+ez/2-camBZ)<=ez/2+2 && ABS(y-camBY)<=3)
	{
		int i,j;
		for(j=0; j<ez; ++j)
		{
			for(i=0; i<ex; ++i)
			{
				draw_quad(x+i,y,z+j,dir,1,1,tex,light);
			}
		}
		return;
	}
	int L=face_light_level(light,dir,skyDarkenCur);
	const u8 *tile=TEX_TILE(tex,L);
	/* Grid lines of the quad's two corners on each axis */
	const V3 *gx[2]={&TX[x],&TX[x+ex]},*gy[2]={&TY[y],&TY[y+1]},*gz[2]={&TZ[z],&TZ[z+ez]};
	/* Texture extent: faces are laid out so that the corners TL,TR,BR,BL
	   map to (0,0),(W,0),(W,H),(0,H) in every direction.  Samples are kept
	   a hair inside the quad so edges never wrap. */
	int W=((dir<=DIR_PX) ? ez : ex)<<20,H=((dir==DIR_NY || dir==DIR_PY) ? ez<<20 : 16<<16);
	int cu[4]={0x200,W-0x200,W-0x200,0x200},cvv[4]={0x200,0x200,H-0x200,H-0x200};
	int flags=texTransparent[tex]|((ex>1 || ez>1) ? RP_WRAP : 0);
	if(g_flatDist && (!texTransparent[tex] || T_LEAVES==tex))
	{
		/* Distant face: the texture's average color (leaves become solid) */
		int nx=CLAMP(camBX,x,x+ex-1)-camBX,nz=CLAMP(camBZ,z,z+ez-1)-camBZ;
		if(sqTab[ABS(nx)]+sqTab[ABS(nz)]>g_flatDist*g_flatDist)
		{
			tile=NULL;
			flags=0;
		}
	}
	CVert cv[4];
	RVert sv[12];
	int k,all=1;
	for(k=0; k<4; ++k)
	{
		const u8 *sel=faceCorner[dir][k];
		const V3 *px=gx[sel[0]],*py=gy[sel[1]],*pz=gz[sel[2]];
		cv[k].x=px->c[0]+py->c[0]+pz->c[0];
		cv[k].y=px->c[1]+py->c[1]+pz->c[1];
		cv[k].z=px->c[2]+py->c[2]+pz->c[2];
		cv[k].u=cu[k];
		cv[k].v=cvv[k];
		if(cv[k].z<NEAR_Z)
		{
			all=0;
		}
	}
	if(all)
	{
		for(k=0; k<4; ++k)
		{
			project(cv[k].x,cv[k].y,cv[k].z,&sv[k].x,&sv[k].y);
			sv[k].u=cv[k].u;
			sv[k].v=cv[k].v;
		}
		draw_spoly(sv,4,tile,flags,g_shadeLUT[L][texAvg[tex]]);
	}
	else
	{
		draw_cpoly(cv,4,tile,flags,g_shadeLUT[L][texAvg[tex]]);
	}
}

static void draw_model_box(int bx,int by,int bz,const u8 *lo,const u8 *hi,const u8 *tex,int L,int topRot)
{
	int d;
	for(d=0; d<6; ++d)
	{
		if(box_face_visible(bx,by,bz,lo,hi,d))
		{
			int l=L-dirShade[d];
			draw_box_face(bx,by,bz,lo,hi,d,tex[d],CLAMP(l,0,15),(d==DIR_PY||d==DIR_NY) ? topRot : 0);
		}
	}
}

static void draw_cross(int bx,int by,int bz,int texId,int L)
{
	/* Two diagonal quads, double sided */
	CVert cv[4];
	int q,k,c;
	static const u8 pts[2][4][3]=
	{
		{{1,16,1},{15,16,15},{15,0,15},{1,0,1}},
		{{15,16,1},{1,16,15},{1,0,15},{15,0,1}},
	};
	for(q=0; q<2; ++q)
	{
		for(k=0; k<4; ++k)
		{
			const u8 *p=pts[q][k];
			const V3 *tx=&TX[bx+(p[0]>>4)],*ty=&TY[by+(p[1]>>4)],*tz=&TZ[bz+(p[2]>>4)];
			for(c=0; c<3; ++c)
			{
				(&cv[k].x)[c]=tx->c[c]+ty->c[c]+tz->c[c]+AX16[p[0]&15].c[c]+AY16[p[1]&15].c[c]+AZ16[p[2]&15].c[c];
			}
			cv[k].u=uvc((k==1||k==2) ? 16 : 0);
			cv[k].v=uvc((k>=2) ? 16 : 0);
		}
		draw_cpoly(cv,4,TEX_TILE(texId,L),1,0);
	}
}

static void draw_model(int bx,int by,int bz,u8 b)
{
	int id=BLK_ID(b),meta=BLK_META(b);
	int L=light_level_at(bx,by,bz,DIR_PY);
	switch(id)
	{
	case B_TORCH:
		{
			static const u8 floorLo[3]={7,0,7},floorHi[3]={9,10,9};
			static const u8 wallLo[4][3]={{0,3,7},{14,3,7},{7,3,0},{7,3,14}};
			u8 tex[6]={T_TORCH,T_TORCH,T_TORCH,T_TORCH,T_TORCH,T_TORCH};
			L=15;
			if(0==meta)
			{
				draw_model_box(bx,by,bz,floorLo,floorHi,tex,L,0);
			}
			else
			{
				u8 lo[3],hi[3];
				int k;
				for(k=0; k<3; ++k)
				{
					lo[k]=wallLo[(meta-1)&3][k];
					hi[k]=lo[k]+(k==1 ? 10 : 2);
				}
				/* Mid-height on the wall.  Texel rows above the flame are transparent. */
				lo[1]=3; hi[1]=13;
				draw_model_box(bx,by,bz,lo,hi,tex,L,0);
			}
		}
		break;
	case B_DOOR_LOWER:
	case B_DOOR_UPPER:
		{
			u8 tex[6],lo[3],hi[3];
			int k,t=(id==B_DOOR_LOWER ? T_DOOR_BOTTOM : T_DOOR_TOP);
			for(k=0; k<6; ++k)
			{
				tex[k]=t;
			}
			tex[DIR_PY]=tex[DIR_NY]=T_PLANKS;
			block_box(b,lo,hi);
			draw_model_box(bx,by,bz,lo,hi,tex,L,0);
		}
		break;
	case B_BED_FOOT:
	case B_BED_HEAD:
		{
			static const u8 lo[3]={0,0,0},hi[3]={16,9,16};
			/* meta: facing direction from foot to head (0 +Z,1 -X,2 -Z,3 +X) */
			static const u8 rotForFacing[4]={2,1,0,3};
			const BlockDef *def=&g_blockDef[id];
			u8 tex[6];
			int k;
			for(k=0; k<6; ++k)
			{
				tex[k]=def->tex[k];
			}
			draw_model_box(bx,by,bz,lo,hi,tex,L,rotForFacing[meta&3]);
		}
		break;
	case B_FLOWER:
	case B_TALLGRASS:
		draw_cross(bx,by,bz,g_blockDef[id].tex[0],CLAMP(L,0,15));
		break;
	}
}

/* ---------- Entity boxes ---------- */

static void draw_mbox(const MBox *mb)
{
	++g_dbg[1];
	int sy=fsin(mb->yaw),cy=fcos(mb->yaw),sp=fsin(mb->pitch),cp=fcos(mb->pitch);
	int ew[3][3],ec[3][3];   /* Local axes in world, then in camera space (2.14) */
	int pivW[3],pivC[3],corner[8][3];
	int a,c,k,d;
	/* Local axes after pitch (about X) and yaw (about Y) */
	ew[0][0]=cy;               ew[0][1]=0;   ew[0][2]=-sy;
	ew[1][0]=(sp*sy)>>14;      ew[1][1]=cp;  ew[1][2]=(sp*cy)>>14;
	ew[2][0]=(cp*sy)>>14;      ew[2][1]=-sp; ew[2][2]=(cp*cy)>>14;
	/* Pivot in world units */
	pivW[0]=mb->ox+((mb->px*16*cy+mb->pz*16*sy)>>14);
	pivW[1]=mb->oy+mb->py*16;
	pivW[2]=mb->oz+((-mb->px*16*sy+mb->pz*16*cy)>>14);
	for(c=0; c<3; ++c)
	{
		pivC[c]=((pivW[0]-g_cam.x)*rot[c][0]+(pivW[1]-g_cam.y)*rot[c][1]+(pivW[2]-g_cam.z)*rot[c][2])>>14;
		for(a=0; a<3; ++a)
		{
			ec[a][c]=(ew[a][0]*rot[c][0]+ew[a][1]*rot[c][1]+ew[a][2]*rot[c][2])>>14;
		}
	}
	for(k=0; k<8; ++k)
	{
		int lx=((k&1) ? mb->x1 : mb->x0)-mb->px;
		int ly=((k&2) ? mb->y1 : mb->y0)-mb->py;
		int lz=((k&4) ? mb->z1 : mb->z0)-mb->pz;
		for(c=0; c<3; ++c)
		{
			corner[k][c]=pivC[c]+((ec[0][c]*lx+ec[1][c]*ly+ec[2][c]*lz)>>10);
		}
	}
	for(d=0; d<6; ++d)
	{
		/* Face corners (as bits of k: x=1,y=2,z=4), TL,TR,BR,BL from outside */
		static const u8 fk[6][4]=
		{
			{6,2,0,4},{3,7,5,1},{4,5,1,0},{2,3,7,6},{2,3,1,0},{7,6,4,5}
		};
		static const int uvx[4]={0,16,16,0},uvy[4]={0,0,16,16};
		int axis=d>>1,sign=(d&1) ? 1 : -1;
		int dot=0,L;
		CVert cv[4];
		for(c=0; c<3; ++c)
		{
			dot+=ec[axis][c]*sign*(corner[fk[d][0]][c]>>2);
		}
		if(dot>=0)
		{
			continue;
		}
		++g_dbg[2];
		for(k=0; k<4; ++k)
		{
			cv[k].x=corner[fk[d][k]][0];
			cv[k].y=corner[fk[d][k]][1];
			cv[k].z=corner[fk[d][k]][2];
			cv[k].u=uvc(uvx[k]);
			cv[k].v=uvc(uvy[k]);
		}
		L=mb->light-dirShade[d];
		L=CLAMP(L,0,15);
		if(mb->flash)
		{
			draw_cpoly(cv,4,NULL,0,P(R_RED,CLAMP(L,4,14)));
		}
		else
		{
			draw_cpoly(cv,4,TEX_TILE(mb->tex[d],L),texTransparent[mb->tex[d]],g_shadeLUT[L][texAvg[mb->tex[d]]]);
		}
	}
}

/* ---------- Frame ---------- */

static inline void plane_values(int *out,const V3 *v,int fpx,int hwp,int hhp)
{
	int x=v->c[0],y=v->c[1],z=v->c[2];
	out[0]=-z;
	out[1]=fpx*x-hwp*z;
	out[2]=-fpx*x-hwp*z;
	out[3]=fpx*y-hhp*z;
	out[4]=-fpx*y-hhp*z;
}

static void setup_camera(void)
{
	int sy=fsin(g_cam.yaw),cy=fcos(g_cam.yaw),sp=fsin(g_cam.pitch),cp=fcos(g_cam.pitch);
	int i,k,x0,x1,z0,z1,pv[NPLANES];
	/* The planes are 1/8 wider than the view so that a collected list stays
	   valid while the camera turns a little (see render_frame) */
	int fpx=focal16>>4,hwp=vw/2+vw/16,hhp=vh/2+vh/16;
	/* right, up, forward */
	rot[0][0]=cy;               rot[0][1]=0;   rot[0][2]=-sy;
	rot[1][0]=-(sy*sp)>>14;     rot[1][1]=cp;  rot[1][2]=-(cy*sp)>>14;
	rot[2][0]=(sy*cp)>>14;      rot[2][1]=sp;  rot[2][2]=(cy*cp)>>14;

	camBX=g_cam.x>>8;
	camBY=g_cam.y>>8;
	camBZ=g_cam.z>>8;

	/* Merged quads can reach 16 cells past the view distance */
	x0=MAX(0,camBX-g_viewDist-18); x1=MIN(g_W,camBX+g_viewDist+18);
	z0=MAX(0,camBZ-g_viewDist-18); z1=MIN(g_W,camBZ+g_viewDist+18);
	for(i=x0; i<=x1; ++i)
	{
		int d=i*256-g_cam.x;
		for(k=0; k<3; ++k)
		{
			TX[i].c[k]=(d*rot[k][0])>>14;
		}
		visX[i]=(d>0 ? 1 : 0)|((d+256)<0 ? 2 : 0);
		plane_values(pv,&TX[i],fpx,hwp,hhp);
		for(k=0; k<NPLANES; ++k)
		{
			PX[k][i]=pv[k];
		}
	}
	for(i=z0; i<=z1; ++i)
	{
		int d=i*256-g_cam.z;
		for(k=0; k<3; ++k)
		{
			TZ[i].c[k]=(d*rot[k][2])>>14;
		}
		visZ[i]=(d>0 ? 16 : 0)|((d+256)<0 ? 32 : 0);
		plane_values(pv,&TZ[i],fpx,hwp,hhp);
		for(k=0; k<NPLANES; ++k)
		{
			PZ[k][i]=pv[k];
		}
	}
	for(i=0; i<=WH; ++i)
	{
		int d=i*256-g_cam.y;
		for(k=0; k<3; ++k)
		{
			TY[i].c[k]=(d*rot[k][1])>>14;
		}
		visY[i]=(d>0 ? 4 : 0)|((d+256)<0 ? 8 : 0);
		plane_values(pv,&TY[i],fpx,hwp,hhp);
		for(k=0; k<NPLANES; ++k)
		{
			PY[k][i]=pv[k];
		}
	}
	planeLen[0]=1;
	planeLen[1]=planeLen[2]=isqrt((u32)(fpx*fpx+hwp*hwp));
	planeLen[3]=planeLen[4]=isqrt((u32)(fpx*fpx+hhp*hhp));
	for(i=0; i<=16; ++i)
	{
		for(k=0; k<3; ++k)
		{
			AX16[i].c[k]=(i*16*rot[k][0])>>14;
			AY16[i].c[k]=(i*16*rot[k][1])>>14;
			AZ16[i].c[k]=(i*16*rot[k][2])>>14;
		}
	}
}

/* Back-to-front order keys (larger = drawn earlier).  Each part is below
   128 (world height 48, view distance at most 40 plus 16 for merged runs),
   so a key is three 7-bit digits. */
static inline int ord_slice(int y)
{
	int d=y-camBY;
	return d<0 ? 1-2*d : 2*d;
}
static inline int ord_axis(int v,int c)
{
	int d=v-c;
	return (d<0 ? 1-2*d : 2*d)+1;   /* 0 is reserved for end-of-row/slice */
}

/* Row axis of the nested order: z (rows of constant z, cells along x) or,
   when looking mostly along x, x.  Chosen per frame so that the side faces
   facing the camera are the ones drawn as merged runs. */
static int nestX;
static inline u32 cell_key(int x,int y,int z)
{
	if(nestX)
	{
		return (ord_slice(y)<<14)|(ord_axis(x,camBX)<<7)|ord_axis(z,camBZ);
	}
	return (ord_slice(y)<<14)|(ord_axis(z,camBZ)<<7)|ord_axis(x,camBX);
}

static inline void add_item(u32 key,u32 a,u32 b)
{
	if(nItems<maxItems)
	{
		items[nItems].a=a;
		items[nItems].b=b;
		itemKey[nItems]=key;
		++nItems;
	}
}

static void sort_items(void)
{
	static u16 cnt[128];
	int pass,i;
	u16 *src=order,*dst=order2,*t;
	for(i=0; i<nItems; ++i)
	{
		src[i]=i;
	}
	for(pass=0; pass<3; ++pass)
	{
		int shift=pass*7;
		u32 sum=0;
		memset(cnt,0,sizeof(cnt));
		for(i=0; i<nItems; ++i)
		{
			++cnt[(itemKey[src[i]]>>shift)&127];
		}
		for(i=0; i<128; ++i)
		{
			u32 c=cnt[i];
			cnt[i]=sum;
			sum+=c;
		}
		for(i=0; i<nItems; ++i)
		{
			u16 k=src[i];
			dst[cnt[(itemKey[k]>>shift)&127]++]=k;
		}
		t=src; src=dst; dst=t;
	}
	if(src!=order)
	{
		memcpy(order,src,2*nItems);
	}
}

/* Plane k of the center of the box spanning grid lines x..x1, y..y1, z..z1,
   times two */
#define PLANE2(k,x,x1,y,y1,z,z1) (PX[k][x]+PX[k][x1]+PY[k][y]+PY[k][y1]+PZ[k][z]+PZ[k][z1])

static void collect(void)
{
	int R=g_viewDist,R2=R*R;
	int ccx0=MAX(0,(camBX-R)>>4),ccx1=MIN(g_NC-1,(camBX+R)>>4);
	int ccz0=MAX(0,(camBZ-R)>>4),ccz1=MIN(g_NC-1,(camBZ+R)>>4);
	int cxi,czi,cyi,dir,grp,k;
	/* Plane thresholds (times two, like PLANE2): chunk bounding sphere,
	   model cell, and quads by the size term ex+ez+1 */
	static int thrQ[NPLANES][34];
	int thrC[NPLANES],thrM[NPLANES];
	for(k=0; k<NPLANES; ++k)
	{
		int i;
		/* Radii grow by the most the camera can move within its cell
		   (443 units) so the list stays valid there */
		int tm=2*443*planeLen[k];
		thrC[k]=2*3548*planeLen[k]+tm;   /* 16*sqrt(3)/2 blocks in units */
		thrM[k]=2*230*planeLen[k]+tm;
		for(i=0; i<34; ++i)
		{
			thrQ[k][i]=2*128*i*planeLen[k]+tm;
		}
	}
	nestX=(ABS(rot[2][0])>ABS(rot[2][2]));
	for(czi=ccz0; czi<=ccz1; ++czi)
	{
		for(cxi=ccx0; cxi<=ccx1; ++cxi)
		{
			/* Horizontal distance from camera to the nearest and farthest
			   cells of the chunk column */
			int x0=cxi*CS,z0=czi*CS,needDist;
			int ddx=0,ddz=0,fdx,fdz;
			if(camBX<x0) ddx=x0-camBX; else if(camBX>=x0+CS) ddx=camBX-(x0+CS-1);
			if(camBZ<z0) ddz=z0-camBZ; else if(camBZ>=z0+CS) ddz=camBZ-(z0+CS-1);
			if(ddx*ddx+ddz*ddz>R2)
			{
				continue;
			}
			fdx=MAX(ABS(camBX-x0),ABS(x0+CS-1-camBX));
			fdz=MAX(ABS(camBZ-z0),ABS(z0+CS-1-camBZ));
			needDist=(fdx*fdx+fdz*fdz>R2);
			for(cyi=0; cyi<NCY; ++cyi)
			{
				Chunk *ch=&g_chunks[chunk_index(cxi,cyi,czi)];
				const u32 *q,*end;
				int y0=cyi*CS,nTest=0,test[NPLANES];
				if(0==ch->count)
				{
					continue;
				}
				/* Frustum: cull the chunk, or find the planes its quads
				   still have to be tested against */
				for(k=0; k<NPLANES; ++k)
				{
					int v=PLANE2(k,x0,x0+CS,y0,y0+CS,z0,z0+CS);
					if(v>thrC[k])
					{
						break;
					}
					if(v>=-thrC[k])
					{
						test[nTest++]=k;
					}
				}
				if(k<NPLANES)
				{
					continue;
				}
				q=g_meshPool+ch->off*2;
				/* Faces, one group (direction and quadrant) at a time.  Groups
				   are sorted with the camera-facing planes first, so stop at
				   the first back-facing one. */
				for(grp=0; grp<NGROUPS; ++grp)
				{
					const u32 *gend=q+ch->group[grp]*2;
					const u8 *box=ch->gbox[grp];
					int gTest[NPLANES],nGTest=0;
					int bx0,bx1,by0,by1,bz0,bz1,t,gNeedDist;
					dir=grp/NSUB;
					if(q==gend)
					{
						continue;
					}
					/* The group's box: distance limit, then the planes the
					   chunk straddles */
					bx0=x0+(box[0]&15); bx1=x0+(box[0]>>4)+1;
					bz0=z0+(box[1]&15); bz1=z0+(box[1]>>4)+1;
					by0=y0+(box[2]&15); by1=y0+(box[2]>>4)+1;
					{
						int nx=CLAMP(camBX,bx0,bx1-1)-camBX,nz=CLAMP(camBZ,bz0,bz1-1)-camBZ;
						int fx=MAX(ABS(bx0-camBX),ABS(bx1-1-camBX)),fz=MAX(ABS(bz0-camBZ),ABS(bz1-1-camBZ));
						if(sqTab[ABS(nx)]+sqTab[ABS(nz)]>R2)
						{
							q=gend;
							continue;
						}
						/* Per quad distance tests only if the box crosses the limit */
						gNeedDist=(needDist && sqTab[fx]+sqTab[fz]>R2);
					}
					for(t=0; t<nTest; ++t)
					{
						int mn,mx,a,b;
						k=test[t];
						a=PX[k][bx0]; b=PX[k][bx1]; mn=MIN(a,b); mx=MAX(a,b);
						a=PY[k][by0]; b=PY[k][by1]; mn+=MIN(a,b); mx+=MAX(a,b);
						a=PZ[k][bz0]; b=PZ[k][bz1]; mn+=MIN(a,b); mx+=MAX(a,b);
						if(mn>0)
						{
							break;      /* Box wholly outside */
						}
						if(mx>0)
						{
							gTest[nGTest++]=k;   /* Straddles: test its quads */
						}
					}
					if(t<nTest)
					{
						q=gend;
						continue;
					}
					for(; q<gend; q+=2)
					{
						u32 w0=q[0],w1=q[1];
						int x=x0+MQ_LX(w0),y=y0+MQ_LY(w0),z=z0+MQ_LZ(w0);
						int ex,ez,i;
						if(0==((visX[x]|visY[y]|visZ[z])&(1<<dir)))
						{
							break;
						}
						ex=(dir<=DIR_PX) ? 1 : MQ_W(w0);
						ez=(dir<=DIR_PY) ? MQ_H(w0) : 1;
						if(gNeedDist)
						{
							/* Nearest point of the quad's footprint for the distance limit */
							int nx=CLAMP(camBX,x,x+ex-1)-camBX;
							int nz=CLAMP(camBZ,z,z+ez-1)-camBZ;
							if(sqTab[ABS(nx)]+sqTab[ABS(nz)]>R2)
							{
								continue;
							}
						}
						/* Bounding sphere of the quad's cells */
						for(i=0; i<nGTest; ++i)
						{
							k=gTest[i];
							if(PLANE2(k,x,x+ex,y,y+1,z,z+ez)>thrQ[k][ex+ez+1])
							{
								break;
							}
						}
						if(i<nGTest)
						{
							continue;
						}
						{
							u32 a=(u32)x|((u32)z<<8)|((u32)y<<16)|((u32)dir<<22)|((u32)IK_QUAD<<25);
							u32 b=w1&0xFFFF;
							if(DIR_PY==dir || DIR_NY==dir)
							{
								/* End of slice */
								add_item(ord_slice(y)<<14,a,b|((u32)(ex-1)<<16)|((u32)(ez-1)<<20));
							}
							else if(dir>=DIR_NZ)
							{
								if(!nestX)
								{
									/* Run along the row: end of row */
									add_item((ord_slice(y)<<14)|(ord_axis(z,camBZ)<<7),a,b|((u32)(ex-1)<<16));
								}
								else
								{
									/* Crosses rows: one item per cell */
									for(i=0; i<ex; ++i)
									{
										add_item(cell_key(x+i,y,z),a+i,b);
									}
								}
							}
							else
							{
								if(nestX)
								{
									add_item((ord_slice(y)<<14)|(ord_axis(x,camBX)<<7),a,b|((u32)(ez-1)<<20));
								}
								else
								{
									for(i=0; i<ez; ++i)
									{
										add_item(cell_key(x,y,z+i),a+((u32)i<<8),b);
									}
								}
							}
						}
					}
					q=gend;
				}
				/* Model cells */
				end=q+ch->group[NGROUPS]*2;
				for(; q<end; q+=2)
				{
					u32 w0=q[0],w1=q[1];
					int x=x0+MQ_LX(w0),y=y0+MQ_LY(w0),z=z0+MQ_LZ(w0);
					int i;
					if(needDist && sqTab[ABS(x-camBX)]+sqTab[ABS(z-camBZ)]>R2)
					{
						continue;
					}
					for(i=0; i<nTest; ++i)
					{
						k=test[i];
						if(PLANE2(k,x,x+1,y,y+1,z,z+1)>thrM[k])
						{
							break;
						}
					}
					if(i<nTest)
					{
						continue;
					}
					add_item(cell_key(x,y,z),(u32)x|((u32)z<<8)|((u32)y<<16)|((u32)IK_MODEL<<25),MQ_BLK(w1));
				}
			}
		}
	}
}

static void draw_sky(const RenderEnv *env)
{
	/* Horizon row: where forward pitch meets 0 elevation */
	int sp=fsin(g_cam.pitch),cp=fcos(g_cam.pitch);
	int horizon=vh/2;
	if(cp>200)
	{
		horizon=vh/2+(int)(((focal16>>4)*sp)/cp);
	}
	raster_fill_rows(0,horizon-vh/10,env->skyColor);
	raster_fill_rows(horizon-vh/10,horizon,(env->skyColor&0xF0)|MIN(15,(env->skyColor&15)+1));
	raster_fill_rows(horizon,vh,env->fogColor);

	/* Stars */
	if(env->starBrightness>0)
	{
		int i;
		for(i=0; i<48; ++i)
		{
			u32 h=hash3(i,7,3,99);
			int yaw=h&1023,el=40+((h>>10)&255);
			int ce=fcos(el),se=fsin(el);
			V3 p;
			int k,sx,sy;
			int d[3]={(fsin(yaw)*ce)>>15,se>>1,(fcos(yaw)*ce)>>15};
			for(k=0; k<3; ++k)
			{
				p.c[k]=(d[0]*rot[k][0]+d[1]*rot[k][1]+d[2]*rot[k][2])>>14;
			}
			if(to_screen(&p,&sx,&sy))
			{
				raster_pixel(sx>>4,sy>>4,P(R_GRAY,MIN(15,env->starBrightness+(h>>20)%3)));
			}
		}
	}

	/* Sun and moon: squares on the celestial circle (east-west) */
	{
		int body;
		for(body=0; body<2; ++body)
		{
			int a=env->sunAngle+body*512;
			int dxw=(fcos(a))>>4,dyw=(fsin(a))>>4;   /* direction, ~1024 */
			int k,s;
			V3 ctr;
			CVert cv[4];
			static const s8 ofs[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
			if(dyw<-200)
			{
				continue;
			}
			for(k=0; k<3; ++k)
			{
				ctr.c[k]=(dxw*16*rot[k][0]+dyw*16*rot[k][1])>>14;
			}
			s=(body ? 1100 : 1500);
			for(k=0; k<4; ++k)
			{
				/* Square spanned by the north axis (z) and the up-tangent */
				int wx=-ofs[k][1]*s*dyw/1024,wy=ofs[k][1]*s*dxw/1024,wz=ofs[k][0]*s;
				int c;
				for(c=0; c<3; ++c)
				{
					(&cv[k].x)[c]=ctr.c[c]+((wx*rot[c][0]+wy*rot[c][1]+wz*rot[c][2])>>14);
				}
				cv[k].u=cv[k].v=0;
			}
			draw_cpoly(cv,4,NULL,0,body ? P(R_GRAY,13) : P(R_FLAME,15));
		}
	}
}

/* Clouds: a flat layer above the world in cells of CLOUD_CELL blocks,
   cloudy or not by a hash, drifting east.  Runs of cloudy cells along x
   are one polygon each.  Everything in the world is below them, so they
   are drawn right after the sky. */
#define CLOUD_CELL 16
#define CLOUD_Y    60          /* Blocks */
#define CLOUD_R    3           /* Cells around the camera */
static void cloud_point(CVert *cv,int wx,int wy,int wz)
{
	int dx=wx-g_cam.x,dy=wy-g_cam.y,dz=wz-g_cam.z,k;
	for(k=0; k<3; ++k)
	{
		(&cv->x)[k]=(dx*rot[k][0]+dy*rot[k][1]+dz*rot[k][2])>>14;
	}
	cv->u=cv->v=0;
}

static void draw_clouds(const RenderEnv *env)
{
	int cell=CLOUD_CELL*256,ccx,ccz,i,j;
	if(!env->cloudColor || env->underwater)
	{
		return;
	}
	/* Cell containing the camera, in the drifting cloud frame */
	ccx=g_cam.x-env->cloudDrift;
	ccx=(ccx>=0) ? ccx/cell : -((cell-1-ccx)/cell);   /* Floor division */
	ccz=g_cam.z/cell;
	for(j=-CLOUD_R; j<=CLOUD_R; ++j)
	{
		for(i=-CLOUD_R; i<=CLOUD_R; )
		{
			int i0;
			if(hash3(ccx+i,0,ccz+j,0xC10D)%100>=35)
			{
				++i;
				continue;
			}
			i0=i;
			while(i<=CLOUD_R && hash3(ccx+i,0,ccz+j,0xC10D)%100<35)
			{
				++i;
			}
			{
				CVert cv[4];
				int x0=(ccx+i0)*cell+env->cloudDrift,x1=(ccx+i)*cell+env->cloudDrift;
				int z0=(ccz+j)*cell,z1=z0+cell,y=CLOUD_Y*256;
				int k,behind=0,left=0,right=0,above=0;
				cloud_point(&cv[0],x0,y,z0);
				cloud_point(&cv[1],x1,y,z0);
				cloud_point(&cv[2],x1,y,z1);
				cloud_point(&cv[3],x0,y,z1);
				/* Cheap rejection before projecting and clipping: all
				   corners behind the camera, off one side or above the top */
				for(k=0; k<4; ++k)
				{
					int zz=MAX(cv[k].z,NEAR_Z);
					behind+=(cv[k].z<NEAR_Z);
					left+=(cv[k].x*focal16<-zz*vw*8);
					right+=(cv[k].x*focal16>zz*vw*8);
					above+=(cv[k].y*focal16>zz*vh*8);
				}
				if(behind<4 && left<4 && right<4 && above<4)
				{
					draw_cpoly(cv,4,NULL,0,env->cloudColor);
				}
			}
		}
	}
}

static void draw_target_outline(const RenderEnv *env)
{
	static const u8 edges[12][2]={{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
	V3 c[8];
	int k,e,ok[8],sx[8],sy[8];
	int x=env->tx,y=env->ty,z=env->tz;
	if(!env->targetValid || !in_world(x,y,z) || ABS(x-camBX)>g_viewDist+1 || ABS(z-camBZ)>g_viewDist+1)
	{
		return;
	}
	for(k=0; k<8; ++k)
	{
		int cc;
		for(cc=0; cc<3; ++cc)
		{
			c[k].c[cc]=TX[x+(k&1)].c[cc]+TY[y+((k>>1)&1)].c[cc]+TZ[z+((k>>2)&1)].c[cc];
		}
		ok[k]=to_screen(&c[k],&sx[k],&sy[k]);
	}
	for(e=0; e<12; ++e)
	{
		int a=edges[e][0],b=edges[e][1];
		if(ok[a] && ok[b])
		{
			raster_line(sx[a],sy[a],sx[b],sy[b],P(R_GRAY,2));
		}
	}
}

void render_frame(u8 *fb,const RenderEnv *env)
{
	int key,ent;
	vw=SCR_W/g_renderScale;
	vh=VIEW_H/g_renderScale;
	focal16=(172*16)/g_renderScale;
	cx16=vw*8;
	cy16=vh*8;
	skyDarkenCur=env->skyDarken;
	if(env->underwater)
	{
		/* Murky: darker lighting and water-colored sky instead of a
		   full-screen tint (which would cost ~10 cycles per pixel) */
		skyDarkenCur=MIN(15,skyDarkenCur+5);
	}
	raster_set_target(fb,vw,vh,FB_PITCH,g_renderScale);
	if(focal16!=invFocal)
	{
		build_inv_table();
	}
	setup_camera();

	g_statFaces=0;
	{
		/* Frame coherence: the collected and sorted face list depends on
		   the camera's cell (order, distance and back-face tests) and,
		   through the frustum, on its orientation.  It is collected with
		   margins (wider planes, radii grown by a cell), so it is reused
		   while the camera stays in its cell, turns less than ~2.8 degrees
		   and no chunk mesh changes. */
		static int cBX=-1,cBY,cBZ,cYaw,cPitch,cScale,cView;
		static u32 cMesh;
		int dyaw=(g_cam.yaw-cYaw)&ANG_MASK;
		if(dyaw>512)
		{
			dyaw=1024-dyaw;
		}
		if(camBX!=cBX || camBY!=cBY || camBZ!=cBZ || g_meshVersion!=cMesh || g_renderScale!=cScale ||
		   g_viewDist!=cView || dyaw>8 || ABS(g_cam.pitch-cPitch)>8)
		{
			u32 t=g_ticks;
			nItems=0;
			collect();
			sort_items();
			nStatic=nItems;
			cBX=camBX; cBY=camBY; cBZ=camBZ;
			cYaw=g_cam.yaw; cPitch=g_cam.pitch;
			cScale=g_renderScale; cView=g_viewDist; cMesh=g_meshVersion;
			g_prof[0]+=g_ticks-t;
		}
	}
	nEnt=0;
	{
		/* Entities span several cells.  Key each one by the cell holding
		   the point of its bounding box nearest the camera: it is then
		   drawn after everything fully behind it and before anything fully
		   in front of it.  Parts of one entity share the key and are
		   inserted near to far, so the stable sort draws far parts first. */
		static u16 idx[MAX_BOXES];
		static int dist[MAX_BOXES];
		int i,j,n=0,R=(g_viewDist+1)*256;
		for(i=0; i<nBoxes; ++i)
		{
			const MBox *b=&boxes[i];
			int px,py,pz;
			if(ABS(b->ox-g_cam.x)>R || ABS(b->oz-g_cam.z)>R)
			{
				continue;
			}
			px=b->ox+((b->px*16*fcos(b->yaw)+b->pz*16*fsin(b->yaw))>>14)-g_cam.x;
			py=b->oy+b->py*16-g_cam.y;
			pz=b->oz+((-b->px*16*fsin(b->yaw)+b->pz*16*fcos(b->yaw))>>14)-g_cam.z;
			dist[i]=(px>>4)*(px>>4)+(py>>4)*(py>>4)+(pz>>4)*(pz>>4);
			for(j=n; j>0 && dist[idx[j-1]]>dist[i]; --j)
			{
				idx[j]=idx[j-1];
			}
			idx[j]=i;
			++n;
		}
		for(j=0; j<n; ++j)
		{
			const MBox *b=&boxes[idx[j]];
			int nx=CLAMP(g_cam.x,b->ox-b->hw,b->ox+b->hw)>>8;
			int ny=CLAMP(g_cam.y,b->oy,b->oy+b->h-1)>>8;
			int nz=CLAMP(g_cam.z,b->oz-b->hw,b->oz+b->hw)>>8;
			int dx=nx-camBX,dz=nz-camBZ;
			if(dx*dx+dz*dz>g_viewDist*g_viewDist)
			{
				continue;
			}
			ny=CLAMP(ny,0,WH-1);
			{
				/* Stable insertion by key (their own small sorted list) */
				u32 k=cell_key(nx,ny,nz);
				int m;
				for(m=nEnt; m>0 && entKey[m-1]>k; --m)
				{
					entKey[m]=entKey[m-1];
					entBox[m]=entBox[m-1];
				}
				entKey[m]=k;
				entBox[m]=idx[j];
				++nEnt;
			}
		}
	}
	g_statItems=nStatic+nEnt;
	g_dbg[0]=nBoxes;

	gfx_wait_flip();   /* First write to the frame buffer */
	{u32 t=g_ticks;
	draw_sky(env);
	draw_clouds(env);
	g_prof[1]+=g_ticks-t;}
	{u32 t=g_ticks;
	/* Far to near: merge the (cached) face list with the entity list.  On
	   equal keys entities go first, as the stable sort used to order them. */
	for(key=nStatic-1,ent=nEnt-1; key>=0 || ent>=0; )
	{
		const Item *it;
		int kind;
		if(ent>=0 && (key<0 || entKey[ent]>=itemKey[order[key]]))
		{
			draw_mbox(&boxes[entBox[ent--]]);
			continue;
		}
		it=&items[order[key--]];
		kind=(it->a>>25)&3;
		{
			int x=it->a&255,z=(it->a>>8)&255,y=(it->a>>16)&63;
			if(IK_MODEL==kind)
			{
				draw_model(x,y,z,(u8)it->b);
			}
			else
			{
				draw_quad(x,y,z,(it->a>>22)&7,((it->b>>16)&15)+1,((it->b>>20)&15)+1,it->b&255,(it->b>>8)&255);
			}
		}
	}
	g_prof[2]+=g_ticks-t;}
	draw_target_outline(env);
	{u32 t=g_ticks;
	raster_finish();
	g_prof[3]+=g_ticks-t;}
}
