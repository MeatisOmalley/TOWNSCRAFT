"""DOS early game: tool wear and the systems built on it (see EARLY_GAME.md).

Each feature is a section of exact, fail-closed replacements. Runtime logic
lives in src/platform/dos/early_game.c; the vendor sources only gain the
fields and hook calls that logic needs.
"""
from build_imported import replace_once

HEADERS = ('blocks.h', 'inventory.h')
NAMES = HEADERS + ('blocks.c', 'inventory.c', 'game.c', 'ui.c')

# Uses before a tool breaks (EARLY_GAME.md, Tools).
DURABILITY = (
    ('I_WOOD_PICK', 60), ('I_WOOD_AXE', 60), ('I_WOOD_SWORD', 60), ('I_WOOD_SHOVEL', 60),
    ('I_STONE_PICK', 130), ('I_STONE_AXE', 130), ('I_STONE_SWORD', 130), ('I_STONE_SHOVEL', 130),
    ('I_IRON_PICK', 600), ('I_IRON_SWORD', 600),
    ('I_DIAMOND_PICK', 1500), ('I_DIAMOND_SWORD', 1500),
)


def durability(name, one):
    if name == 'inventory.h':
        one('\tu8 item,count;\n} Slot;',
            '\tu8 item,count;\n\tu16 wear;           /* Uses so far; only tools wear */\n} Slot;')
        one('void inv_take_from_slot(int slot,int count);',
            'void inv_take_from_slot(int slot,int count);\n'
            'int inv_add_slot(const Slot *s);      /* Keeps tool wear; returns the count not placed */')
    elif name == 'blocks.h':
        one('\tu8 placeBlock;      /* Block placed, 0 if none */\n} ItemDef;',
            '\tu8 placeBlock;      /* Block placed, 0 if none */\n'
            '\tu16 durability;     /* Uses before it breaks, 0 if it never wears */\n} ItemDef;')
    elif name == 'blocks.c':
        lines = ''.join(f'\tg_itemDef[{item}].durability={uses};\n' for item, uses in DURABILITY)
        one('\tdef_item(I_FEATHER,"Feather",T_I_FEATHER,TOOL_NONE,0,1,0,64,0);\n',
            '\tdef_item(I_FEATHER,"Feather",T_I_FEATHER,TOOL_NONE,0,1,0,64,0);\n' + lines)
    elif name == 'inventory.c':
        one('\t\t\tg_inv[i].item=item;\n\t\t\tg_inv[i].count=n;\n',
            '\t\t\tg_inv[i].item=item;\n\t\t\tg_inv[i].count=n;\n\t\t\tg_inv[i].wear=0;\n')
        one('int inv_count(int item)\n',
            'int inv_add_slot(const Slot *s)\n{\n\tint i;\n'
            '\tif(!s->item || !s->count) return 0;\n'
            '\tif(!s->wear) return inv_add(s->item,s->count);\n'
            '\t/* A worn tool keeps its wear: it moves whole into an empty slot. */\n'
            '\tfor(i=0; i<INV_SLOTS; ++i)\n'
            '\t\tif(!g_inv[i].item) { g_inv[i]=*s; return 0; }\n'
            '\treturn s->count;\n}\n\n'
            'int inv_count(int item)\n')
        one('\t\t\tlost+=inv_add(c->slot[i].item,c->slot[i].count);',
            '\t\t\tlost+=inv_add_slot(&c->slot[i]);')
    elif name == 'game.c':
        one('#include "inventory.h"\n', '#include "inventory.h"\n#include "early_game.h"\n')
        one('\tif(def->needTier && (tool!=def->tool || tier<def->needTier))\n\t{\n\t\tdrop=0;\n\t}\n',
            '\tif(def->needTier && (tool!=def->tool || tier<def->needTier))\n\t{\n\t\tdrop=0;\n\t}\n'
            '\t/* A tool wears once per block, twice when it is the wrong tool. */\n'
            '\tif(TOOL_NONE!=tool && def->hardness>1) dos_tool_wear(tool==def->tool ? 1 : 2);\n')
        one('\t\tmob_hurt(i,dmg,g_player.body.x,g_player.body.z,1);\n\t\tattackCooldown=10;\n',
            '\t\tmob_hurt(i,dmg,g_player.body.x,g_player.body.z,1);\n\t\tattackCooldown=10;\n'
            '\t\tdos_tool_wear(TOOL_SWORD==g_itemDef[held_item()].tool ? 1 : 2);\n')
        one('if(!slot->item) {slot->item=invHeld.item;slot->count=1;--invHeld.count;}',
            'if(!slot->item) {slot->item=invHeld.item;slot->count=1;slot->wear=invHeld.wear;--invHeld.count;}')
        one('\t\tinvHeld.item=slot->item;\n\t\tinvHeld.count=(slot->count+1)/2;\n',
            '\t\tinvHeld.item=slot->item;\n\t\tinvHeld.count=(slot->count+1)/2;\n\t\tinvHeld.wear=slot->wear;\n')
        one('\t\t\tinvHeld.count=inv_add(invHeld.item,invHeld.count);\n',
            '\t\t\tinvHeld.count=inv_add_slot(&invHeld);\n')
        one('\t\tinv_add(invHeld.item,invHeld.count);\n\t\tinvHeld.item=invHeld.count=0;\n',
            '\t\tinv_add_slot(&invHeld);\n\t\tinvHeld.item=invHeld.count=0;\n')
    elif name == 'ui.c':
        one('\t\tif(s->count>1)\n\t\t{\n\t\t\tui_small_number(fb,x+19,y+14,s->count,C_WHITE);\n\t\t}\n',
            '\t\tif(s->count>1)\n\t\t{\n\t\t\tui_small_number(fb,x+19,y+14,s->count,C_WHITE);\n\t\t}\n'
            '\t\tif(s->wear && g_itemDef[s->item].durability)\n\t\t{\n'
            '\t\t\t/* Remaining uses: green, then yellow, then red. */\n'
            '\t\t\tint d=g_itemDef[s->item].durability,left=d-s->wear;\n'
            '\t\t\tgfx_rect(fb,x+2,y+16,16,2,C_BLACK);\n'
            '\t\t\tgfx_rect(fb,x+2,y+16,MAX(1,left*16/d),1,\n'
            '\t\t\t\tleft*3>d*2 ? P(R_GRASS,12) : (left*3>d ? C_YELLOW : C_RED));\n'
            '\t\t}\n')


def adapt(name, data):
    if name not in NAMES:
        return data, []
    text = data.decode()
    recipe = []
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')

    def one(before, after):
        nonlocal text
        text = replace_once(text, before, after, recipe)

    durability(name, one)
    return text.encode(), recipe
