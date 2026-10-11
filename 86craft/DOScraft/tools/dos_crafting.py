"""DOS-only combined inventory/crafting menu over the immutable Towns import."""

from build_imported import replace_once


MENU = r'''enum { MENU_OUTPUT=45 };

static int menu_grid_width(void) { return craftStation==ST_TABLE ? 3 : 2; }

static Slot *menu_slot(int id)
{
	if(id>=0 && id<36) return &g_inv[inv_slot_at(id)];
	if(id>=36 && id<45) return &dos_craft_grid[id-36];
	return 0;
}

static int menu_slot_xy(int id,int *x,int *y)
{
	if(id>=0 && id<36)
	{
		int row=id/9;
		*x=70+(id%9)*20;
		*y=row==3 ? 180 : 108+row*20;
		return 1;
	}
	if(id>=36 && id<45 && (id-36)/3<menu_grid_width() &&
	   (id-36)%3<menu_grid_width())
	{
		*x=92+((id-36)%3)*20;
		*y=29+((id-36)/3)*20;
		return 1;
	}
	if(id==MENU_OUTPUT) { *x=194; *y=49; return 1; }
	return 0;
}

static int menu_hit(int x,int y)
{
	int i,sx,sy;
	for(i=0;i<=MENU_OUTPUT;++i)
		if(menu_slot_xy(i,&sx,&sy) && x>=sx && x<sx+20 && y>=sy && y<sy+20)
			return i;
	return -1;
}

static void menu_activate(int id)
{
	Slot *slot=menu_slot(id);
	if(id==MENU_OUTPUT)
	{
		if(!dos_craft_grid_take(&invHeld,craftStation,menu_grid_width()))
			game_message("No recipe or no space");
		else sound_play(SFX_CLICK,256,120,0);
		return;
	}
	if(slot)
	{
		Slot old=*slot;
		if(invHeld.item && old.item==invHeld.item)
		{
			int n=MIN(invHeld.count,g_itemDef[old.item].maxStack-old.count);
			slot->count+=n;
			invHeld.count-=n;
			if(!invHeld.count) invHeld.item=0;
		}
		else { *slot=invHeld; invHeld=old; }
		sound_play(SFX_CLICK,256,120,0);
	}
}

static void menu_move(int from,int to)
{
	Slot *src=menu_slot(from),*dst=menu_slot(to);
	if(!src || !dst || !src->item || from==to) return;
	if(dst->item==src->item)
	{
		int n=MIN(src->count,g_itemDef[src->item].maxStack-dst->count);
		dst->count+=n;
		src->count-=n;
		if(!src->count) src->item=0;
	}
	else { Slot old=*dst; *dst=*src; *src=old; }
	sound_play(SFX_CLICK,256,120,0);
}

static void menu_right(int id)
{
	Slot *slot=menu_slot(id);
	if(id==MENU_OUTPUT) { menu_activate(id); return; }
	if(!slot) return;
	if(invHeld.item)
	{
		if(!slot->item) {slot->item=invHeld.item;slot->count=1;--invHeld.count;}
		else if(slot->item==invHeld.item && slot->count<g_itemDef[slot->item].maxStack)
			{++slot->count;--invHeld.count;}
		if(!invHeld.count) invHeld.item=0;
	}
	else if(slot->item)
	{
		invHeld.item=slot->item;
		invHeld.count=(slot->count+1)/2;
		slot->count-=invHeld.count;
		if(!slot->count) slot->item=0;
	}
}

static void inventory_mouse(int pressed,int released,int dx,int dy)
{
	int hit;
	menuPointerX=CLAMP(menuPointerX+dx,60,257);
	menuPointerY=CLAMP(menuPointerY+dy,10,227);
	hit=menu_hit(menuPointerX,menuPointerY);
	if(pressed&MOUSE_R) menu_right(hit);
	if(pressed&MOUSE_L) menuDragSource=hit;
	if(released&MOUSE_L)
	{
		int from=menuDragSource;
		menuDragSource=-1;
		if(hit<0) return;
		if(invHeld.item)
		{
			if(hit!=MENU_OUTPUT) menu_activate(hit);
		}
		else if(from==MENU_OUTPUT)
		{
			Slot *dst=hit==MENU_OUTPUT ? &invHeld : menu_slot(hit);
			if(dst && (hit==MENU_OUTPUT || hit<36) &&
			   dos_craft_grid_take(dst,craftStation,menu_grid_width()))
				sound_play(SFX_CLICK,256,120,0);
		}
		else if(from>=0)
		{
			if(from==hit) menu_activate(from);
			else if(hit!=MENU_OUTPUT) menu_move(from,hit);
		}
		if(hit<36) {menuFocus=0;invCursor=hit;}
		else if(hit<45) {menuFocus=1;craftCursor=hit-36;}
		else menuFocus=2;
	}
}

static void draw_inventory(void)
{
	const Recipe *recipe=dos_craft_grid_match(craftStation,menu_grid_width());
	Slot output={0,0};
	int i,x,y,hover=menu_hit(menuPointerX,menuPointerY);
	ui_panel(g_fb,58,8,204,224);
	gfx_text_shadow(g_fb,70,14,craftStation==ST_TABLE ? "CRAFTING TABLE" : "CRAFTING",C_YELLOW,C_BLACK);
	for(i=0;i<9;++i) if(menu_slot_xy(36+i,&x,&y))
		ui_slot(g_fb,x,y,&dos_craft_grid[i],menuFocus==1 && craftCursor==i);
	gfx_text_shadow(g_fb,163,55,">",C_WHITE,C_BLACK);
	if(recipe) {output.item=recipe->out;output.count=recipe->outCount;}
	ui_slot(g_fb,194,49,&output,menuFocus==2);
	gfx_text_shadow(g_fb,70,96,"INVENTORY",C_YELLOW,C_BLACK);
	for(i=0;i<36;++i)
	{
		menu_slot_xy(i,&x,&y);
		ui_slot(g_fb,x,y,&g_inv[inv_slot_at(i)],menuFocus==0 && invCursor==i);
	}
	if(hover>=0 && menu_slot_xy(hover,&x,&y))
		gfx_frame(g_fb,x-1,y-1,22,22,C_YELLOW);
	if(menuDragSource>=0 && menu_slot_xy(menuDragSource,&x,&y))
		gfx_frame(g_fb,x-2,y-2,24,24,C_WHITE);
	if(invHeld.item)
	{
		ui_icon(g_fb,70,203,invHeld.item);
		gfx_text_shadow(g_fb,91,206,g_itemDef[invHeld.item].name,C_WHITE,C_BLACK);
	}
	else if(hover>=0 && hover<45 && menu_slot(hover) && menu_slot(hover)->item)
		gfx_text_shadow(g_fb,70,206,g_itemDef[menu_slot(hover)->item].name,C_WHITE,C_BLACK);
	else if(recipe)
		gfx_text_shadow(g_fb,70,206,g_itemDef[recipe->out].name,C_WHITE,C_BLACK);
	gfx_text_shadow(g_fb,70,219,"L drag  R one  C grid",P(R_GRAY,11),C_BLACK);
	/* The DOS mouse is relative in play; menus keep a small independent cursor. */
	gfx_rect(g_fb,menuPointerX,menuPointerY,3,3,C_WHITE);
}

static void close_inventory(void)
{
	if(state==GS_INVENTORY)
	{
		if(invHeld.item)
		{
			invHeld.count=inv_add(invHeld.item,invHeld.count);
			if(!invHeld.count) invHeld.item=0;
		}
		if(!dos_craft_grid_return() || invHeld.item)
		{
			game_message("Make room for crafting items");
			return;
		}
	}
	else if(invHeld.item)
	{
		inv_add(invHeld.item,invHeld.count);
		invHeld.item=invHeld.count=0;
	}
	state=GS_PLAY;
}

static void inventory_key(int k)
{
	int width=menu_grid_width();
	if(k==KEY_ESC || k==KEY_TAB) {close_inventory();return;}
	if(k==KEY_C) {menuFocus=menuFocus==0 ? 1 : 0;return;}
	if(k==KEY_Q)
	{
		Slot *s=menuFocus==0 ? menu_slot(invCursor) :
			(menuFocus==1 ? menu_slot(36+craftCursor) : 0);
		if(invHeld.item) invHeld.item=invHeld.count=0;
		else if(s) s->item=s->count=0;
		return;
	}
	if(k==KEY_SPACE || k==KEY_RETURN || k==KEY_E || k==KEY_NUM_RETURN)
	{
		menu_activate(menuFocus==0 ? invCursor :
			(menuFocus==1 ? 36+craftCursor : MENU_OUTPUT));
		return;
	}
	if(menuFocus==0)
	{
		switch(k)
		{
		case KEY_LEFT: case KEY_A: case KEY_NUM_4: if(invCursor%9) --invCursor; break;
		case KEY_RIGHT: case KEY_D: case KEY_NUM_6: if(invCursor%9<8) ++invCursor; break;
		case KEY_UP: case KEY_W: case KEY_NUM_8:
			if(invCursor>=9) invCursor-=9;
			else {menuFocus=1;craftCursor=MIN(invCursor%9,width-1);}
			break;
		case KEY_DOWN: case KEY_S: case KEY_NUM_2: if(invCursor<27) invCursor+=9; break;
		}
	}
	else if(menuFocus==1)
	{
		switch(k)
		{
		case KEY_LEFT: case KEY_A: case KEY_NUM_4: if(craftCursor%3) --craftCursor; break;
		case KEY_RIGHT: case KEY_D: case KEY_NUM_6:
			if(craftCursor%3<width-1) ++craftCursor;
			else menuFocus=2;
			break;
		case KEY_UP: case KEY_W: case KEY_NUM_8: if(craftCursor>=3) craftCursor-=3; break;
		case KEY_DOWN: case KEY_S: case KEY_NUM_2:
			if(craftCursor/3<width-1) craftCursor+=3;
			else {menuFocus=0;invCursor=craftCursor%3;}
			break;
		}
	}
	else
	{
		if(k==KEY_LEFT || k==KEY_A || k==KEY_NUM_4)
			{menuFocus=1;craftCursor=width-1;}
		else if(k==KEY_DOWN || k==KEY_S || k==KEY_NUM_2)
			{menuFocus=0;invCursor=4;}
	}
}

'''


def adapt(name, data):
    if name != 'game.c':
        return data, []
    recipe = []
    text = data.decode('utf-8')
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n',
                           count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    text = replace_once(text, '#include "inventory.h"\n',
                        '#include "inventory.h"\n#include "craft_grid.h"\n', recipe)
    text = replace_once(text, 'static Slot invHeld;\n',
        'static Slot invHeld;\n'
        'static int menuFocus,craftCursor,menuPointerX=160,menuPointerY=120,menuDragSource=-1;\n'
        'static void open_inventory(int station,int focus)\n'
        '{\n'
        '\tcraftStation=station;\n\tmenuFocus=focus;\n\tcraftCursor=0;\n'
        '\tinvCursor=27+g_player.selected;\n\tmenuPointerX=160;\n\tmenuPointerY=120;\n'
        '\tmenuDragSource=-1;\n\tdos_craft_grid_clear();\n\tstate=GS_INVENTORY;\n}\n', recipe)
    text = replace_once(text,
        '\t\tstate=GS_CRAFT;\n\t\tcraftStation=ST_TABLE;\n\t\tcraftSel=craftScroll=0;\n\t\treturn 1;',
        '\t\topen_inventory(ST_TABLE,1);\n\t\treturn 1;', recipe)
    text = replace_once(text,
        '\t\tstate=GS_INVENTORY;\n\t\tinvCursor=27+g_player.selected;\n\t\tbreak;',
        '\t\topen_inventory(ST_HAND,0);\n\t\tbreak;', recipe)
    text = replace_once(text,
        '\t\tstate=GS_CRAFT;\n\t\tcraftStation=ST_HAND;\n\t\tcraftSel=craftScroll=0;\n\t\tbreak;',
        '\t\topen_inventory(ST_HAND,1);\n\t\tbreak;', recipe)
    start = text.index('static void draw_inventory(void)\n')
    end = text.index('/* ---------------- Settings ---------------- */', start)
    text = replace_once(text, text[start:end], MENU, recipe)
    text = replace_once(text,
        '\t\tint mousePressed=mouse&~mousePrev;\n',
        '\t\tint mousePressed=mouse&~mousePrev;\n'
        '\t\tint mouseReleased=mousePrev&~mouse;\n', recipe)
    text = replace_once(text, '\t\t/* Game pad: SELECT cycles the hotbar */',
        '\t\tif(GS_INVENTORY==state)\n'
        '\t\t\tinventory_mouse(mousePressed,mouseReleased,mouseDX,mouseDY);\n'
        '\t\t/* Game pad: SELECT cycles the hotbar */', recipe)
    text = replace_once(text, '"TAB         Inventory",',
                        '"TAB         Inventory + craft",', recipe)
    text = replace_once(text, '"C           Craft (by hand)",',
                        '"C           Open craft grid",', recipe)
    text = replace_once(text, '"Tables/Furnaces: more recipes.",',
                        '"Table: 3x3 grid. Furnace: smelt.",', recipe)
    text = replace_once(text, '"Mouse (port B): look, L/R buttons",',
                        '"Mouse: look / menu drag, R one",', recipe)
    return text.encode('utf-8'), recipe
