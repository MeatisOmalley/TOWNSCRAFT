"""DOS early game: tool wear and the systems built on it (see EARLY_GAME.md).

Each feature is a section of exact, fail-closed replacements. Runtime logic
lives in src/platform/dos/early_game.c; the vendor sources only gain the
fields and hook calls that logic needs.
"""
from build_imported import replace_once

HEADERS = ('blocks.h', 'inventory.h', 'textures.h')
NAMES = HEADERS + ('blocks.c', 'inventory.c', 'game.c', 'ui.c', 'textures.c')

# The olive ramp becomes terracotta: red clay, bricks and copper (EARLY_GAME.md, Textures).
OLIVE = (95, 155, 85)
TERRACOTTA = (205, 112, 72)
PALETTE = ('\t{%3d,%3d,%3d}, /* R_OLIVE  */' % OLIVE, '\t{%3d,%3d,%3d}, /* R_TERRA  */' % TERRACOTTA)


def swap_palette(text):
    """Only the palette change, for native builds of the unaltered art."""
    if text.count(PALETTE[0]) != 1:
        raise ValueError('Expected the pinned olive ramp')
    return text.replace(*PALETTE)
# Tiles from tools/early_game_textures.py. Shaded (world) tiles come before the
# item icons, because every texture before T_I_STICK gets a lit atlas page.
BLOCK_TEXTURES = ('T_CLAY', 'T_COPPER_ORE', 'T_FIRE_LOG', 'T_FIRE_FLAME', 'T_FIRE_ASH',
                  'T_ANVIL_TOP', 'T_ANVIL_SIDE', 'T_CLAYF_FRONT', 'T_CLAYF_SIDE', 'T_CLAYF_TOP',
                  'T_BRICKS', 'T_BLOOMERY_FRONT', 'T_BLOOMERY_SIDE')
ITEM_TEXTURES = ('T_I_CLAY_LUMP', 'T_I_BRICK', 'T_I_FLINT', 'T_I_CHARCOAL', 'T_I_COPPER_INGOT',
                 'T_I_TIN', 'T_I_BRONZE_INGOT', 'T_I_STICK_SPEAR', 'T_I_FLINT_SPEAR',
                 'T_I_COPPER_SPEAR', 'T_I_BRONZE_SPEAR', 'T_I_IRON_SPEAR', 'T_I_COPPER_PICK',
                 'T_I_COPPER_AXE', 'T_I_COPPER_SHOVEL', 'T_I_IRON_AXE', 'T_I_IRON_SHOVEL',
                 'T_I_DIAMOND_AXE', 'T_I_DIAMOND_SHOVEL', 'T_I_FIRE', 'T_I_BELLOWS',
                 'T_I_BRONZE_PICK', 'T_I_BRONZE_AXE', 'T_I_BRONZE_SHOVEL', 'T_I_BRONZE_SWORD')

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


def textures(name, one, every):
    if name == 'blocks.h':
        one('\tT_CHICKEN_SKIN,T_CHICKEN_FACE,T_CHICKEN_YELLOW,\n',
            '\tT_CHICKEN_SKIN,T_CHICKEN_FACE,T_CHICKEN_YELLOW,\n\t' + ','.join(BLOCK_TEXTURES) + ',\n')
        one('\tT_I_CHICKEN,T_I_COOKED_CHICKEN,T_I_FEATHER,\n',
            '\tT_I_CHICKEN,T_I_COOKED_CHICKEN,T_I_FEATHER,\n\t' + ','.join(ITEM_TEXTURES) + ',\n')
    elif name == 'textures.h':
        one('#define P(ramp,shade) ((ramp)*16+(shade))',
            '#define P(ramp,shade) ((ramp)*16+(shade))\n'
            '#define R_TERRA R_OLIVE    /* The olive ramp holds terracotta */')
    elif name == 'textures.c':
        one(*PALETTE)
        # Zombies and rotten flesh keep a green cast on the leaf ramp.
        every('noisefill(R_OLIVE,11,2);', 'noisefill(R_LEAF,11,2);', 2)
        one('P(R_OLIVE,8)', 'P(R_LEAF,8)')
        one('P(R_OLIVE,6)', 'P(R_LEAF,6)')
        one('gen_meat(R_OLIVE,10)', 'gen_meat(R_LEAF,10)')
        one('#include "../../../assets/textures/eagler_simple.inc"\n',
            '#include "../../../assets/textures/eagler_simple.inc"\n'
            '#include "../../../assets/textures/early_game.inc"\n')
        one('\tif(dos_texture_detail) dos_tex_import_full();\n\telse dos_tex_import_simple();\n',
            '\tif(dos_texture_detail) dos_tex_import_full();\n\telse dos_tex_import_simple();\n'
            '\t/* Early-game tiles have one style at every resolution. */\n'
            '\tdos_tex_import_early();\n')


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

    def every(before, after, count):
        nonlocal text
        if text.count(before) != count:
            raise ValueError(f'Expected {count} copies of {before!r}')
        recipe.append(dict(operation='replace', before=before, after=after, count=count))
        text = text.replace(before, after)

    durability(name, one)
    textures(name, one, every)
    return text.encode(), recipe
