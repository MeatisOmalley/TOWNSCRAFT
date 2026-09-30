#ifndef UI_H
#define UI_H
#include "common.h"
#include "inventory.h"

void ui_icon(u8 *fb,int x,int y,int item);
void ui_small_number(u8 *fb,int x,int y,int n,u8 color);  /* Right-aligned at x */
void ui_slot(u8 *fb,int x,int y,const Slot *s,int highlight);
void ui_hearts(u8 *fb,int x,int y,int health,int blink);
void ui_bubbles(u8 *fb,int x,int y,int n);
void ui_hotbar(u8 *fb,int selected);
/* HUD strip below the 3D view (air<0: no bubbles).  Redrawn only when
   changed on that VRAM page; ui_hud_invalidate() after drawing over it. */
void ui_hud_strip(u8 *fb,int page,int selected,int health,int blink,int air);
void ui_hud_invalidate(void);
void ui_crosshair(u8 *fb);
void ui_text_scaled(u8 *fb,int x,int y,const char *s,u8 color,int scale);
void ui_panel(u8 *fb,int x,int y,int w,int h);

#endif
