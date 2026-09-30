/* Palette and procedurally generated 16x16 textures.

   The palette is 16 color ramps x 16 shades.  Index = ramp*16+shade.
   Brightness is exponential in the shade (each step ~0.8x), so lighting a
   texel is a subtraction of shades, which keeps the texture's contrast.
   Texel 0 means transparent. */
#include "textures.h"
#include "video.h"
#include "sys.h"

u8 g_tex[NUM_TEXTURES][256];
u8 *g_texAtlas;              /* Page per texture: 16 rows x (16 light levels x 16 texels) */
u8 g_shadeLUT[16][256];      /* [light][palette index] */

static const u8 rampRGB[16][3]=
{
	{235,235,235}, /* R_GRAY   */
	{150,108, 74}, /* R_DIRT   */
	{105,180, 55}, /* R_GRASS  */
	{ 55,135, 40}, /* R_LEAF   */
	{230,215,155}, /* R_SAND   */
	{110, 85, 52}, /* R_BARK   */
	{190,155, 98}, /* R_PLANK  */
	{ 55, 95,230}, /* R_WATER  */
	{140,190,255}, /* R_SKY    */
	{210, 40, 40}, /* R_RED    */
	{245,165,165}, /* R_PINK   */
	{255,205, 60}, /* R_FLAME  */
	{ 40,175,175}, /* R_CYAN   */
	{ 70, 70,175}, /* R_BLUE   */
	{225,180,145}, /* R_PEACH  */
	{ 95,155, 85}, /* R_OLIVE  */
};

/* Brightness of shade s in 1/256: 0.02 + 0.98*0.85^(15-s) */
static const u16 shadeBright[16]=
{
	27,31,35,41,47,55,63,73,86,100,116,136,159,186,218,256
};

void palette_apply(void)
{
	int r,s;
	for(r=0; r<16; ++r)
	{
		for(s=0; s<16; ++s)
		{
			int b=shadeBright[s],c;
			int rgb[3];
			for(c=0; c<3; ++c)
			{
				rgb[c]=MIN(255,rampRGB[r][c]*b>>8);
			}
			video_set_palette(r*16+s,rgb[0],rgb[1],rgb[2]);
		}
	}
}

/* ---- drawing helpers ---- */
static u8 *T;
static int seedBase;

static int hn(int x,int y,int n)
{
	return (int)(hash3(x,y,seedBase,0x9E37+n)>>8);
}
static void px(int x,int y,int c)
{
	if(0<=x && x<16 && 0<=y && y<16)
	{
		T[y*16+x]=(u8)c;
	}
}
static int gp(int x,int y)
{
	return T[(y&15)*16+(x&15)];
}
static void noisefill(int ramp,int base,int range)
{
	int x,y;
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			int s=base+hn(x,y,1)%range-range/2;
			px(x,y,P(ramp,CLAMP(s,1,15)));
		}
	}
}
static void speckle(int ramp,int shade,int pct,int n)
{
	int x,y;
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			if(hn(x,y,n)%100<pct)
			{
				px(x,y,P(ramp,shade));
			}
		}
	}
}
static void rect(int x0,int y0,int x1,int y1,int c)
{
	int x,y;
	for(y=y0; y<=y1; ++y)
	{
		for(x=x0; x<=x1; ++x)
		{
			px(x,y,c);
		}
	}
}
static void line(int x0,int y0,int x1,int y1,int c)
{
	int dx=ABS(x1-x0),dy=ABS(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy;
	for(;;)
	{
		px(x0,y0,c);
		if(x0==x1 && y0==y1)
		{
			break;
		}
		{
			int e2=2*err;
			if(e2>-dy){err-=dy;x0+=sx;}
			if(e2<dx){err+=dx;y0+=sy;}
		}
	}
}
static void disc(int cx,int cy,int r2,int c)
{
	int x,y;
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			int dx=2*x+1-cx,dy=2*y+1-cy;   /* cx,cy in half pixels */
			if(dx*dx+dy*dy<=r2)
			{
				px(x,y,c);
			}
		}
	}
}
/* Shaded disc: shade varies with position for a rounded look */
static void blob(int cx,int cy,int r2,int ramp,int base)
{
	int x,y;
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			int dx=2*x+1-cx,dy=2*y+1-cy;
			if(dx*dx+dy*dy<=r2)
			{
				int s=base-(dx+dy)/6+hn(x,y,7)%2;
				px(x,y,P(ramp,CLAMP(s,1,15)));
			}
		}
	}
}
static void begin(int id)
{
	T=g_tex[id];
	seedBase=id*131;
	memset(T,0,256);
}

/* ---- block textures ---- */
static void gen_stone(void)
{
	noisefill(R_GRAY,11,3);
	speckle(R_GRAY,9,8,3);
}
static void gen_dirt(void)
{
	noisefill(R_DIRT,12,4);
	speckle(R_DIRT,9,8,3);
	speckle(R_DIRT,14,5,4);
}
static void gen_cobble(void)
{
	int cx[7],cy[7],cs[7],i,x,y;
	for(i=0; i<7; ++i)
	{
		cx[i]=hn(i,0,11)%16;
		cy[i]=hn(i,1,11)%16;
		cs[i]=10+hn(i,2,11)%4;
	}
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			int best=0,bestD=9999,second=9999;
			for(i=0; i<7; ++i)
			{
				int dx=ABS(x-cx[i]),dy=ABS(y-cy[i]),d;
				if(dx>8) dx=16-dx;
				if(dy>8) dy=16-dy;
				d=dx*dx+dy*dy;
				if(d<bestD){second=bestD;bestD=d;best=i;}
				else if(d<second){second=d;}
			}
			if(second-bestD<4)
			{
				px(x,y,P(R_GRAY,6));
			}
			else
			{
				px(x,y,P(R_GRAY,cs[best]-(bestD>12)+hn(x,y,2)%2));
			}
		}
	}
}
static void gen_planks(int ramp,int dark)
{
	int x,y;
	for(y=0; y<16; ++y)
	{
		int board=y/4,seam=(board*7+3)%16;
		for(x=0; x<16; ++x)
		{
			int s=12-dark+((x*3+board*5+hn(x/3,board,3))%3)-1;
			if(3==(y&3))
			{
				s=8-dark;
			}
			else if(x==seam)
			{
				s=9-dark;
			}
			px(x,y,P(ramp,CLAMP(s,1,15)));
		}
	}
}
static void gen_log_side(void)
{
	int x,y;
	for(x=0; x<16; ++x)
	{
		int col=hn(x,0,5)%3;
		for(y=0; y<16; ++y)
		{
			int s=10+col+(hn(x,y/3,6)%2);
			if(0==hn(x,y,7)%9)
			{
				s=7;
			}
			px(x,y,P(R_BARK,s));
		}
	}
}
static void gen_log_top(void)
{
	int x,y;
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			int d=MAX(ABS(2*x-15),ABS(2*y-15))/2;
			if(d>=7)
			{
				px(x,y,P(R_BARK,10+hn(x,y,1)%2));
			}
			else
			{
				px(x,y,P(R_PLANK,(d&1) ? 10 : 13));
			}
		}
	}
}
static void gen_grass_side(void)
{
	int x;
	gen_dirt();
	for(x=0; x<16; ++x)
	{
		int h=3+hn(x,0,9)%3,y;
		for(y=0; y<h; ++y)
		{
			px(x,y,P(R_GRASS,11+hn(x,y,1)%3));
		}
	}
}
static void gen_glass(void)
{
	int i;
	for(i=0; i<16; ++i)
	{
		px(i,0,P(R_SKY,14));
		px(i,15,P(R_SKY,12));
		px(0,i,P(R_SKY,14));
		px(15,i,P(R_SKY,12));
	}
	line(3,6,6,3,P(R_GRAY,15));
	line(3,8,8,3,P(R_GRAY,14));
	line(9,12,12,9,P(R_GRAY,15));
}
static void gen_ore(int ramp,int shade)
{
	int i;
	gen_stone();
	for(i=0; i<5; ++i)
	{
		int cx=3+hn(i,0,21)%10,cy=3+hn(i,1,21)%10,k;
		for(k=0; k<5; ++k)
		{
			int x=cx+hn(i,k,22)%3-1,y=cy+hn(k,i,23)%3-1;
			px(x,y,P(ramp,shade+hn(x,y,1)%2));
			px(x+1,y,P(ramp,shade));
		}
	}
}
static void gen_craft_top(void)
{
	gen_planks(R_PLANK,0);
	rect(0,0,15,0,P(R_BARK,8));
	rect(0,15,15,15,P(R_BARK,8));
	rect(0,0,0,15,P(R_BARK,8));
	rect(15,0,15,15,P(R_BARK,8));
	rect(5,1,5,14,P(R_BARK,9));
	rect(10,1,10,14,P(R_BARK,9));
	rect(1,5,14,5,P(R_BARK,9));
	rect(1,10,14,10,P(R_BARK,9));
}
static void gen_craft_side(int front)
{
	gen_planks(R_PLANK,1);
	rect(0,0,15,2,P(R_BARK,9));
	rect(0,1,15,1,P(R_PLANK,12));
	if(front)
	{
		/* Saw */
		rect(3,6,9,8,P(R_GRAY,12));
		line(3,9,9,9,P(R_GRAY,9));
		rect(10,6,12,7,P(R_BARK,9));
	}
	else
	{
		/* Hammer */
		rect(8,5,12,7,P(R_GRAY,11));
		line(10,8,6,13,P(R_BARK,9));
		line(10,9,7,13,P(R_BARK,10));
	}
}
static void gen_furnace(int face)
{
	int x,y;
	noisefill(R_GRAY,10,3);
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			if(0==x || 15==x || 0==y || 15==y)
			{
				px(x,y,P(R_GRAY,8));
			}
		}
	}
	if(1==face)
	{
		rect(4,8,11,13,P(R_GRAY,2));
		rect(5,12,10,13,P(R_FLAME,11));
		rect(6,11,9,11,P(R_FLAME,13));
		rect(3,4,12,5,P(R_GRAY,7));
	}
	else if(2==face)
	{
		noisefill(R_GRAY,11,2);
	}
}
static void gen_torch(void)
{
	rect(7,6,8,15,P(R_PLANK,10));
	px(7,9,P(R_BARK,9));
	px(8,12,P(R_BARK,9));
	rect(7,6,8,7,P(R_FLAME,15));
	rect(7,8,8,8,P(R_FLAME,12));
}
static void gen_door(int top)
{
	int x,y;
	gen_planks(R_BARK,-3);
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			if(x<2 || x>13 || (top ? y<2 : y>13))
			{
				px(x,y,P(R_BARK,9));
			}
		}
	}
	if(top)
	{
		rect(3,3,7,8,0);
		rect(8,3,12,8,0);
		rect(7,3,8,8,P(R_BARK,9));
		rect(3,9,12,10,P(R_BARK,9));
	}
	else
	{
		rect(4,2,11,6,P(R_BARK,10));
		rect(4,8,11,12,P(R_BARK,10));
		px(12,1,P(R_GRAY,12));
	}
}
static void gen_bed_top(int head)
{
	int x,y;
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			int s=11+(hn(x,y,1)%2);
			if(0==x || 15==x)
			{
				s=9;
			}
			px(x,y,P(R_RED,s));
		}
	}
	if(head)
	{
		rect(2,2,13,7,P(R_GRAY,14));
		rect(2,7,13,7,P(R_GRAY,12));
	}
	else
	{
		rect(0,13,15,15,P(R_RED,9));
	}
}
static void gen_bed_side(int head)
{
	int x;
	rect(0,7,15,9,P(R_RED,11));
	if(head)
	{
		rect(0,7,5,9,P(R_GRAY,14));
	}
	rect(0,10,15,12,P(R_PLANK,11));
	rect(0,13,2,15,P(R_PLANK,9));
	rect(13,13,15,15,P(R_PLANK,9));
	for(x=0; x<16; ++x)
	{
		px(x,10,P(R_PLANK,9));
	}
}
static void gen_wool(void)
{
	int x,y;
	noisefill(R_GRAY,14,2);
	for(y=0; y<16; y+=4)
	{
		for(x=0; x<16; ++x)
		{
			if(0==(x+y/4)%3)
			{
				px(x,y+(x&1),P(R_GRAY,12));
			}
		}
	}
}
static void gen_gravel(void)
{
	int x,y;
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			int h=hn(x/2,y/2,1)%6;
			int c=(h<2 ? P(R_GRAY,9+h) : (h<4 ? P(R_GRAY,12+h%2) : P(R_DIRT,10+h%2)));
			px(x,y,c);
		}
	}
}
static void gen_flower(void)
{
	line(8,15,8,7,P(R_GRASS,9));
	line(8,12,10,10,P(R_GRASS,10));
	disc(16,10,20,P(R_RED,12));
	px(7,4,P(R_FLAME,15));
	px(8,4,P(R_FLAME,14));
	px(7,5,P(R_FLAME,14));
	px(8,5,P(R_FLAME,15));
}
static void gen_tallgrass(void)
{
	int i;
	for(i=0; i<7; ++i)
	{
		int x=1+i*2+hn(i,0,1)%2,h=5+hn(i,1,1)%8;
		int lean=hn(i,2,1)%3-1;
		line(x,15,x+lean,15-h,P(R_GRASS,10+(i&1)));
	}
}
static void gen_stonebrick(void)
{
	int x,y;
	noisefill(R_GRAY,11,2);
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			int off=(y<8 ? 0 : 8);
			if(7==(y&7) || 0==((x+off)&15) || 15==((x+off)&15))
			{
				px(x,y,P(R_GRAY,7));
			}
		}
	}
}
static void gen_tnt(int top)
{
	int x;
	noisefill(R_RED,11,2);
	if(top)
	{
		rect(5,5,10,10,P(R_GRAY,5));
		rect(7,7,8,8,P(R_GRAY,2));
		return;
	}
	for(x=0; x<16; x+=4)
	{
		rect(x,0,x,15,P(R_RED,9));
	}
	rect(0,5,15,10,P(R_GRAY,15));
	/* "TNT" */
	rect(1,6,3,6,P(R_GRAY,2)); rect(2,7,2,9,P(R_GRAY,2));
	rect(6,6,6,9,P(R_GRAY,2)); rect(9,6,9,9,P(R_GRAY,2)); px(7,7,P(R_GRAY,2)); px(8,8,P(R_GRAY,2));
	rect(12,6,14,6,P(R_GRAY,2)); rect(13,7,13,9,P(R_GRAY,2));
}

/* ---- mob textures ---- */
static void eyes(int y,int ramp,int shade)
{
	rect(3,y,5,y+1,P(R_GRAY,15));
	rect(10,y,12,y+1,P(R_GRAY,15));
	rect(4,y,5,y+1,P(ramp,shade));
	rect(10,y,11,y+1,P(ramp,shade));
}
static void gen_pig_face(void)
{
	noisefill(R_PINK,12,2);
	eyes(5,R_GRAY,2);
	rect(5,8,10,11,P(R_PINK,14));
	px(6,10,P(R_PINK,7));
	px(9,10,P(R_PINK,7));
}
static void gen_sheep_face(void)
{
	noisefill(R_GRAY,14,2);
	rect(3,3,12,15,P(R_PEACH,12));
	eyes(6,R_GRAY,2);
	rect(6,11,9,12,P(R_PINK,11));
}
static void gen_zombie_face(void)
{
	noisefill(R_OLIVE,11,2);
	rect(3,6,5,7,P(R_GRAY,2));
	rect(10,6,12,7,P(R_GRAY,2));
	rect(7,9,8,9,P(R_OLIVE,8));
	rect(5,11,10,12,P(R_OLIVE,6));
}
static void gen_creeper_skin(void)
{
	int x,y;
	for(y=0; y<16; ++y)
	{
		for(x=0; x<16; ++x)
		{
			int h=hn(x/2,y/2,1)%5;
			px(x,y,P(R_GRASS,8+h+(hn(x,y,2)%2)));
		}
	}
}
static void gen_creeper_face(void)
{
	gen_creeper_skin();
	rect(3,4,6,7,P(R_GRAY,1));
	rect(9,4,12,7,P(R_GRAY,1));
	rect(6,8,9,12,P(R_GRAY,1));
	rect(5,10,5,14,P(R_GRAY,1));
	rect(10,10,10,14,P(R_GRAY,1));
}

/* ---- item icons ---- */
static void handle(void)
{
	line(3,13,10,6,P(R_BARK,10));
	line(4,13,11,6,P(R_PLANK,9));
}
static void tool_color(int tier,int *ramp,int *shade)
{
	switch(tier)
	{
	case 1: *ramp=R_PLANK; *shade=12; break;
	case 2: *ramp=R_GRAY;  *shade=10; break;
	default:*ramp=R_GRAY;  *shade=14; break;
	}
}
static void gen_pick(int tier)
{
	int r,s;
	tool_color(tier,&r,&s);
	handle();
	line(3,3,6,2,P(r,s));
	line(6,2,9,3,P(r,s));
	line(9,3,12,6,P(r,s));
	line(12,6,13,9,P(r,s));
	line(4,4,6,3,P(r,s-2));
	line(9,4,11,6,P(r,s-2));
	line(11,7,12,9,P(r,s-2));
}
static void gen_sword(int tier)
{
	int r,s;
	tool_color(tier,&r,&s);
	line(5,10,13,2,P(r,s));
	line(6,10,13,3,P(r,s-2));
	line(4,8,8,12,P(R_BARK,8));
	line(2,14,5,11,P(R_BARK,10));
}
static void gen_axe(int tier)
{
	int r,s;
	tool_color(tier,&r,&s);
	handle();
	rect(8,2,11,5,P(r,s));
	rect(7,3,7,6,P(r,s));
	rect(8,6,9,6,P(r,s-2));
	rect(11,2,11,5,P(r,s-2));
}
static void gen_shovel(int tier)
{
	int r,s;
	tool_color(tier,&r,&s);
	handle();
	disc(24,8,16,P(r,s));
	px(12,3,P(r,s-2));
	px(12,4,P(r,s-2));
}
static void gen_meat(int ramp,int shade)
{
	blob(16,17,70,ramp,shade);
	line(5,9,10,7,P(ramp,shade+2));
	rect(2,10,4,11,P(R_GRAY,14));
}

void textures_init(void)
{
	int t;
	begin(T_STONE); gen_stone();
	begin(T_GRASS_TOP); noisefill(R_GRASS,12,4); speckle(R_GRASS,10,10,2);
	begin(T_GRASS_SIDE); gen_grass_side();
	begin(T_DIRT); gen_dirt();
	begin(T_COBBLE); gen_cobble();
	begin(T_PLANKS); gen_planks(R_PLANK,0);
	begin(T_LOG_SIDE); gen_log_side();
	begin(T_LOG_TOP); gen_log_top();
	begin(T_LEAVES); noisefill(R_LEAF,11,5); speckle(R_LEAF,6,14,2); speckle(R_LEAF,13,6,3);
	begin(T_SAND); noisefill(R_SAND,13,3); speckle(R_SAND,11,6,2);
	begin(T_WATER); noisefill(R_WATER,11,2);
	{
		int y;
		for(y=2; y<16; y+=5)
		{
			line(hn(y,0,1)%6,y,6+hn(y,1,1)%8,y,P(R_WATER,13));
		}
	}
	begin(T_GLASS); gen_glass();
	begin(T_BEDROCK); noisefill(R_GRAY,6,7);
	begin(T_COAL_ORE); gen_ore(R_GRAY,2);
	begin(T_IRON_ORE); gen_ore(R_PEACH,12);
	begin(T_CRAFT_TOP); gen_craft_top();
	begin(T_CRAFT_SIDE); gen_craft_side(0);
	begin(T_CRAFT_FRONT); gen_craft_side(1);
	begin(T_FURNACE_FRONT); gen_furnace(1);
	begin(T_FURNACE_SIDE); gen_furnace(0);
	begin(T_FURNACE_TOP); gen_furnace(2);
	begin(T_TORCH); gen_torch();
	begin(T_DOOR_BOTTOM); gen_door(0);
	begin(T_DOOR_TOP); gen_door(1);
	begin(T_BED_FOOT_TOP); gen_bed_top(0);
	begin(T_BED_HEAD_TOP); gen_bed_top(1);
	begin(T_BED_SIDE); gen_bed_side(0);
	begin(T_BED_HEAD_SIDE); gen_bed_side(1);
	begin(T_BED_END); gen_bed_side(0); rect(3,7,12,9,P(R_RED,10));
	begin(T_WOOL); gen_wool();
	begin(T_GRAVEL); gen_gravel();
	begin(T_FLOWER); gen_flower();
	begin(T_TALLGRASS); gen_tallgrass();
	begin(T_STONEBRICK); gen_stonebrick();
	begin(T_TNT_SIDE); gen_tnt(0);
	begin(T_TNT_TOP); gen_tnt(1);

	begin(T_PIG_SKIN); noisefill(R_PINK,12,2);
	begin(T_PIG_FACE); gen_pig_face();
	begin(T_SHEEP_WOOL); gen_wool();
	begin(T_SHEEP_FACE); gen_sheep_face();
	begin(T_ZOMBIE_SKIN); noisefill(R_OLIVE,11,2);
	begin(T_ZOMBIE_FACE); gen_zombie_face();
	begin(T_SHIRT); noisefill(R_CYAN,11,2);
	begin(T_PANTS); noisefill(R_BLUE,10,2);
	begin(T_CREEPER_SKIN); gen_creeper_skin();
	begin(T_CREEPER_FACE); gen_creeper_face();
	begin(T_PLAYER_SKIN); noisefill(R_PEACH,12,2);

	begin(T_I_STICK); line(3,12,12,3,P(R_BARK,10)); line(4,12,12,4,P(R_PLANK,10));
	begin(T_I_COAL); blob(16,16,60,R_GRAY,4); speckle(R_GRAY,0,0,1);
	begin(T_I_IRON); rect(3,6,12,10,P(R_GRAY,13)); rect(4,6,12,6,P(R_GRAY,15)); rect(3,10,12,10,P(R_GRAY,10));
	begin(T_I_WOOD_PICK); gen_pick(1);
	begin(T_I_STONE_PICK); gen_pick(2);
	begin(T_I_IRON_PICK); gen_pick(3);
	begin(T_I_WOOD_SWORD); gen_sword(1);
	begin(T_I_STONE_SWORD); gen_sword(2);
	begin(T_I_IRON_SWORD); gen_sword(3);
	begin(T_I_WOOD_AXE); gen_axe(1);
	begin(T_I_STONE_AXE); gen_axe(2);
	begin(T_I_WOOD_SHOVEL); gen_shovel(1);
	begin(T_I_STONE_SHOVEL); gen_shovel(2);
	begin(T_I_PORK); gen_meat(R_PINK,12);
	begin(T_I_COOKED_PORK); gen_meat(R_DIRT,11);
	begin(T_I_MUTTON); gen_meat(R_RED,11);
	begin(T_I_COOKED_MUTTON); gen_meat(R_BARK,11);
	begin(T_I_FLESH); gen_meat(R_OLIVE,10); speckle(R_RED,9,10,5);
	begin(T_I_GUNPOWDER); disc(16,20,50,P(R_GRAY,8)); speckle(R_GRAY,5,30,3); rect(0,0,15,6,0);
	begin(T_I_DOOR);
	{
		int x,y;
		for(y=1; y<15; ++y)
		{
			for(x=4; x<12; ++x)
			{
				px(x,y,P(R_BARK,(x==4||x==11||y==1||y==14||y==7) ? 8 : 11));
			}
		}
		rect(5,2,10,6,P(R_SKY,12));
		px(10,9,P(R_GRAY,13));
	}
	begin(T_I_BED); rect(1,7,14,10,P(R_RED,11)); rect(1,6,4,9,P(R_GRAY,14)); rect(1,11,14,11,P(R_PLANK,10)); rect(1,12,1,13,P(R_PLANK,9)); rect(14,12,14,13,P(R_PLANK,9));
	begin(T_I_APPLE); blob(16,19,60,R_RED,12); line(8,2,8,5,P(R_BARK,9)); px(9,3,P(R_LEAF,11)); px(10,3,P(R_LEAF,11));
	begin(T_I_TORCH); rect(7,5,8,14,P(R_PLANK,10)); rect(7,2,8,4,P(R_FLAME,15)); px(6,3,P(R_FLAME,13)); px(9,3,P(R_FLAME,13));
	begin(T_CRACK);
	{
		line(2,3,7,8,P(R_GRAY,2)); line(7,8,13,6,P(R_GRAY,2)); line(7,8,6,14,P(R_GRAY,2)); line(10,7,12,12,P(R_GRAY,2));
	}

	/* Shaded copies of world textures, one atlas page per texture */
	g_texAtlas=heap_alloc_low(NUM_SHADED_TEXTURES*4096);
	for(t=0; t<16; ++t)
	{
		int i;
		for(i=0; i<256; ++i)
		{
			int s=(i&15)-(15-t);
			g_shadeLUT[t][i]=(0==i ? 0 : (u8)((i&0xF0)|CLAMP(s,1,15)));
		}
	}
	for(t=0; t<NUM_SHADED_TEXTURES; ++t)
	{
		int L,i;
		for(L=0; L<16; ++L)
		{
			for(i=0; i<256; ++i)
			{
				g_texAtlas[t*4096+(i>>4)*256+L*16+(i&15)]=g_shadeLUT[L][g_tex[t][i]];
			}
		}
	}
	(void)gp;
}

/* Average color of a texture (for tiny distant faces) */
u8 texture_average(int id)
{
	int i,n=0,sumS=0,ramp=0,rampN[16];
	memset(rampN,0,sizeof(rampN));
	for(i=0; i<256; ++i)
	{
		u8 c=g_tex[id][i];
		if(c)
		{
			rampN[c>>4]++;
			sumS+=c&15;
			++n;
		}
	}
	for(i=1; i<16; ++i)
	{
		if(rampN[i]>rampN[ramp])
		{
			ramp=i;
		}
	}
	return n ? (u8)(ramp*16+sumS/n) : 0;
}
