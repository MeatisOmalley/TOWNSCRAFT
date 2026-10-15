/* HUD and menu drawing helpers. */
#include "ui.h"
#include "gfx.h"
#include "textures.h"
#include "inventory.h"
#include "player.h"

extern const u8 g_font8x8[95][8];

void ui_icon(u8 *fb,int x,int y,int item)
{
	const u8 *t;
	int i,j;
	if(item<=0 || item>=NUM_ITEMS)
	{
		return;
	}
	t=g_tex[g_itemDef[item].icon];
	for(j=0; j<16; ++j)
	{
		int yy=y+j;
		u8 *row;
		if(yy<0 || yy>=SCR_H)
		{
			continue;
		}
		row=fb+yy*FB_PITCH;
		for(i=0; i<16; ++i)
		{
			u8 c=t[j*16+i];
			int xx=x+i;
			if(c && xx>=0 && xx<SCR_W)
			{
				row[xx]=c;
			}
		}
	}
}

/* Small 3x5 digits for stack counts */
static const u16 digits3x5[10]=
{
	0x7B6F,0x2C97,0x73E7,0x73CF,0x5BC9,0x79CF,0x79EF,0x7249,0x7BEF,0x7BCF
};

void ui_small_number(u8 *fb,int x,int y,int n,u8 color)
{
	char buf[12];
	int i,len;
	itoa_dec(n,buf);
	len=strlen(buf);
	x-=len*4;
	for(i=0; i<len; ++i)
	{
		int d=buf[i]-'0',r,c;
		if(d<0 || d>9)
		{
			continue;
		}
		for(r=0; r<5; ++r)
		{
			for(c=0; c<3; ++c)
			{
				if((digits3x5[d]>>(14-(r*3+c)))&1)
				{
					int xx=x+i*4+c,yy=y+r;
					if(xx>=0 && xx<SCR_W && yy>=0 && yy<SCR_H)
					{
						fb[yy*FB_PITCH+xx]=color;
						if(xx+1<SCR_W && yy+1<SCR_H)
						{
							u8 *sh=&fb[(yy+1)*FB_PITCH+xx+1];
							if(*sh!=color) *sh=C_BLACK;
						}
					}
				}
			}
		}
	}
}

void ui_slot(u8 *fb,int x,int y,const Slot *s,int highlight)
{
	gfx_rect(fb,x,y,20,20,P(R_GRAY,5));
	gfx_frame(fb,x,y,20,20,highlight ? C_WHITE : P(R_GRAY,9));
	if(highlight)
	{
		gfx_frame(fb,x+1,y+1,18,18,C_WHITE);
	}
	if(s && s->item)
	{
		ui_icon(fb,x+2,y+2,s->item);
		if(s->count>1)
		{
			ui_small_number(fb,x+19,y+14,s->count,C_WHITE);
		}
	}
}

static void heart(u8 *fb,int x,int y,int fill)
{
	static const u8 shape[7]={0x36,0x7F,0x7F,0x7F,0x3E,0x1C,0x08};
	int r,c;
	for(r=0; r<7; ++r)
	{
		for(c=0; c<7; ++c)
		{
			if((shape[r]>>(6-c))&1)
			{
				u8 col;
				if(2==fill || (1==fill && c<4))
				{
					col=(r<2 && c>0 && c<3) ? P(R_PINK,15) : C_RED;
				}
				else
				{
					col=P(R_GRAY,3);
				}
				fb[(y+r)*FB_PITCH+x+c]=col;
			}
		}
	}
}

void ui_hearts(u8 *fb,int x,int y,int health,int blink)
{
	int i;
	for(i=0; i<10; ++i)
	{
		int hp=health-i*2;
		int fill=(hp>=2) ? 2 : (hp==1 ? 1 : 0);
		int yy=y+((blink && (i&1)) ? -1 : 0);
		heart(fb,x+i*8,yy,fill);
	}
}

void ui_bubbles(u8 *fb,int x,int y,int n)
{
	int i;
	for(i=0; i<n; ++i)
	{
		gfx_frame(fb,x+i*8+1,y,5,5,P(R_SKY,15));
		gfx_rect(fb,x+i*8+2,y+1,3,3,P(R_WATER,12));
	}
}

void ui_hotbar(u8 *fb,int selected)
{
	int i,x0=(SCR_W-9*20)/2,y0=SCR_H-22;
	for(i=0; i<HOTBAR; ++i)
	{
		ui_slot(fb,x0+i*20,y0,&g_inv[i],i==selected);
	}
}

/* HUD strip below the 3D view: hearts, air bubbles and the hotbar.  The
   world is not drawn there, so each VRAM page keeps its strip and it is
   redrawn only when its contents change. */
typedef struct
{
	Slot slots[HOTBAR];
	int selected,health,blink,air;
} HudKey;
static HudKey hudKey[3];
static int hudValid[3];

void ui_hud_invalidate(void)
{
	hudValid[0]=hudValid[1]=hudValid[2]=0;
}

void ui_hud_strip(u8 *fb,int page,int selected,int health,int blink,int air)
{
	HudKey k;
	int x0=(SCR_W-9*20)/2;
	memset(&k,0,sizeof(k));
	memcpy(k.slots,g_inv,sizeof(k.slots));
	k.selected=selected;
	k.health=health;
	k.blink=blink;
	k.air=air;
	if(hudValid[page] && 0==memcmp(&k,&hudKey[page],sizeof(k)))
	{
		return;
	}
	gfx_rect(fb,0,VIEW_H,SCR_W,SCR_H-VIEW_H,P(R_GRAY,2));
	ui_hearts(fb,x0,SCR_H-32,health,blink);
	if(air>=0)
	{
		ui_bubbles(fb,x0+100,SCR_H-32,air);
	}
	ui_hotbar(fb,selected);
	hudKey[page]=k;
	hudValid[page]=1;
}

void ui_crosshair(u8 *fb)
{
	int cx=SCR_W/2,cy=VIEW_H/2,i;
	for(i=-4; i<=4; ++i)
	{
		u8 *p=&fb[cy*FB_PITCH+cx+i];
		u8 *q=&fb[(cy+i)*FB_PITCH+cx];
		*p=g_darkenLUT[*p]^0x0F;
		if(i)
		{
			*q=g_darkenLUT[*q]^0x0F;
		}
	}
}

void ui_text_scaled(u8 *fb,int x,int y,const char *s,u8 color,int scale)
{
	for(; *s; ++s,x+=8*scale)
	{
		int ch=*s,r,c;
		const u8 *g;
		if(ch<0x20 || ch>0x7E)
		{
			continue;
		}
		g=g_font8x8[ch-0x20];
		for(r=0; r<8; ++r)
		{
			for(c=0; c<8; ++c)
			{
				if((g[r]>>c)&1)
				{
					gfx_rect(fb,x+c*scale,y+r*scale,scale,scale,color);
				}
			}
		}
	}
}

void ui_panel(u8 *fb,int x,int y,int w,int h)
{
	gfx_rect(fb,x,y,w,h,P(R_GRAY,3));
	gfx_frame(fb,x,y,w,h,P(R_GRAY,11));
	gfx_frame(fb,x+1,y+1,w-2,h-2,P(R_GRAY,5));
}
