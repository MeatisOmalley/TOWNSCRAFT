"""DOS early game: tool wear and the systems built on it (see EARLY_GAME.md).

Each feature is a section of exact, fail-closed replacements. Runtime logic
lives in src/platform/dos/early_game.c; the vendor sources only gain the
fields and hook calls that logic needs.
"""
from build_imported import replace_once

HEADERS = ('blocks.h', 'inventory.h', 'textures.h')
NAMES = HEADERS + ('blocks.c', 'inventory.c', 'game.c', 'ui.c', 'textures.c', 'world.c', 'render.c')

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


NEW_BLOCKS = ('B_CLAY', 'B_COPPER_ORE', 'B_FIRE', 'B_FIRE_OUT', 'B_ANVIL', 'B_CLAY_FURNACE',
              'B_BRICKS', 'B_BLOOMERY')
NEW_ITEMS = ('I_CLAY_LUMP', 'I_BRICK', 'I_FLINT', 'I_CHARCOAL', 'I_COPPER_INGOT', 'I_TIN',
             'I_BRONZE_INGOT', 'I_STICK_SPEAR', 'I_FLINT_SPEAR', 'I_COPPER_SPEAR', 'I_BRONZE_SPEAR',
             'I_IRON_SPEAR', 'I_COPPER_PICK', 'I_COPPER_AXE', 'I_COPPER_SHOVEL', 'I_BRONZE_PICK',
             'I_BRONZE_AXE', 'I_BRONZE_SHOVEL', 'I_BRONZE_SWORD', 'I_IRON_AXE', 'I_IRON_SHOVEL',
             'I_DIAMOND_AXE', 'I_DIAMOND_SHOVEL', 'I_BELLOWS')
# Light opacity per new block, in NEW_BLOCKS order.
NEW_OPACITY = (15, 15, 0, 0, 15, 15, 15, 15)
BLOCK_ROWS = (
    '\t{"Clay",       CUBEOPQ,                            0, 40,  TOOL_SHOVEL,0, I_CLAY_LUMP,   {SIDES(T_CLAY)}},\n'
    '\t{"Copper Ore", CUBEOPQ,                            0, 50,  TOOL_PICK,  1, B_COPPER_ORE,  {SIDES(T_COPPER_ORE)}},\n'
    '\t{"Campfire",   BF_MODEL|BF_USABLE,                14,  5,  TOOL_NONE,  0, 0,             '
    '{T_FIRE_LOG,T_FIRE_LOG,T_FIRE_ASH,T_FIRE_LOG,T_FIRE_LOG,T_FIRE_LOG}},\n'
    '\t{"Fire Pit",   BF_MODEL|BF_USABLE,                 0,  5,  TOOL_NONE,  0, 0,             {SIDES(T_FIRE_ASH)}},\n'
    '\t{"Stone Anvil",CUBEOPQ|BF_USABLE,                  0, 45,  TOOL_PICK,  0, B_ANVIL,       '
    '{T_ANVIL_SIDE,T_ANVIL_SIDE,T_ANVIL_SIDE,T_ANVIL_TOP,T_ANVIL_SIDE,T_ANVIL_SIDE}},\n'
    '\t{"Clay Furnace",CUBEOPQ|BF_USABLE,                 0, 40,  TOOL_PICK,  0, B_CLAY_FURNACE,'
    '{T_CLAYF_SIDE,T_CLAYF_SIDE,T_CLAYF_TOP,T_CLAYF_TOP,T_CLAYF_FRONT,T_CLAYF_FRONT}},\n'
    '\t{"Bricks",     CUBEOPQ,                            0, 45,  TOOL_PICK,  1, B_BRICKS,      {SIDES(T_BRICKS)}},\n'
    '\t{"Bloomery",   CUBEOPQ|BF_USABLE,                  0, 45,  TOOL_PICK,  0, B_BLOOMERY,    '
    '{T_BLOOMERY_SIDE,T_BLOOMERY_SIDE,T_CLAYF_TOP,T_CLAYF_TOP,T_BLOOMERY_FRONT,T_BLOOMERY_FRONT}},\n'
)
# Tool tiers: hand 0, stone/wood 1, copper 2, bronze 3, iron 4, diamond 5.
RETIER = (('I_WOOD_PICK', 1), ('I_STONE_PICK', 1), ('I_IRON_PICK', 4), ('I_DIAMOND_PICK', 5),
          ('I_WOOD_SWORD', 1), ('I_STONE_SWORD', 1), ('I_IRON_SWORD', 4), ('I_DIAMOND_SWORD', 5),
          ('I_WOOD_AXE', 1), ('I_STONE_AXE', 1), ('I_WOOD_SHOVEL', 1), ('I_STONE_SHOVEL', 1))
# (item, name, icon, tool, tier, damage, durability)
NEW_TOOLS = (
    ('I_STICK_SPEAR', 'Stick Spear', 'T_I_STICK_SPEAR', 'TOOL_SPEAR', 1, 1, 59),
    ('I_FLINT_SPEAR', 'Flint Spear', 'T_I_FLINT_SPEAR', 'TOOL_SPEAR', 1, 2, 131),
    ('I_COPPER_SPEAR', 'Copper Spear', 'T_I_COPPER_SPEAR', 'TOOL_SPEAR', 2, 2, 190),
    ('I_BRONZE_SPEAR', 'Bronze Spear', 'T_I_BRONZE_SPEAR', 'TOOL_SPEAR', 3, 3, 450),
    ('I_IRON_SPEAR', 'Iron Spear', 'T_I_IRON_SPEAR', 'TOOL_SPEAR', 4, 3, 600),
    ('I_COPPER_PICK', 'Copper Pickaxe', 'T_I_COPPER_PICK', 'TOOL_PICK', 2, 3, 190),
    ('I_COPPER_AXE', 'Copper Axe', 'T_I_COPPER_AXE', 'TOOL_AXE', 2, 4, 190),
    ('I_COPPER_SHOVEL', 'Copper Shovel', 'T_I_COPPER_SHOVEL', 'TOOL_SHOVEL', 2, 2, 190),
    ('I_BRONZE_PICK', 'Bronze Pickaxe', 'T_I_BRONZE_PICK', 'TOOL_PICK', 3, 3, 450),
    ('I_BRONZE_AXE', 'Bronze Axe', 'T_I_BRONZE_AXE', 'TOOL_AXE', 3, 4, 450),
    ('I_BRONZE_SHOVEL', 'Bronze Shovel', 'T_I_BRONZE_SHOVEL', 'TOOL_SHOVEL', 3, 2, 450),
    ('I_BRONZE_SWORD', 'Bronze Sword', 'T_I_BRONZE_SWORD', 'TOOL_SWORD', 3, 5, 450),
    ('I_IRON_AXE', 'Iron Axe', 'T_I_IRON_AXE', 'TOOL_AXE', 4, 5, 600),
    ('I_IRON_SHOVEL', 'Iron Shovel', 'T_I_IRON_SHOVEL', 'TOOL_SHOVEL', 4, 3, 600),
    ('I_DIAMOND_AXE', 'Diamond Axe', 'T_I_DIAMOND_AXE', 'TOOL_AXE', 5, 6, 1500),
    ('I_DIAMOND_SHOVEL', 'Diamond Shovel', 'T_I_DIAMOND_SHOVEL', 'TOOL_SHOVEL', 5, 4, 1500),
)
MATERIALS = (('I_CLAY_LUMP', 'Clay Lump', 'T_I_CLAY_LUMP'), ('I_BRICK', 'Clay Brick', 'T_I_BRICK'),
             ('I_FLINT', 'Flint', 'T_I_FLINT'), ('I_CHARCOAL', 'Charcoal', 'T_I_CHARCOAL'),
             ('I_COPPER_INGOT', 'Copper Ingot', 'T_I_COPPER_INGOT'), ('I_TIN', 'Tin', 'T_I_TIN'),
             ('I_BRONZE_INGOT', 'Bronze Ingot', 'T_I_BRONZE_INGOT'))


def materials(name, one):
    if name == 'blocks.h':
        one('\tB_WALL_TORCH,B_DOOR_LOWER_OPEN,B_DOOR_UPPER_OPEN,\n\tNUM_BLOCKS',
            '\tB_WALL_TORCH,B_DOOR_LOWER_OPEN,B_DOOR_UPPER_OPEN,\n'
            '\t/* Early game */\n\t' + ','.join(NEW_BLOCKS) + ',\n\tNUM_BLOCKS')
        one('\tI_RAW_CHICKEN,I_COOKED_CHICKEN,I_FEATHER,\n\tNUM_ITEMS',
            '\tI_RAW_CHICKEN,I_COOKED_CHICKEN,I_FEATHER,\n\t' + ','.join(NEW_ITEMS) + ',\n\tNUM_ITEMS')
        one('TOOL_NONE,TOOL_PICK,TOOL_AXE,TOOL_SHOVEL,TOOL_SWORD',
            'TOOL_NONE,TOOL_PICK,TOOL_AXE,TOOL_SHOVEL,TOOL_SWORD,TOOL_SPEAR')
        one('\tu8 tier;            /* 1 wood, 2 stone, 3 iron */',
            '\tu8 tier;            /* 1 stone/wood, 2 copper, 3 bronze, 4 iron, 5 diamond */')
    elif name == 'blocks.c':
        one('{SIDES(T_DOOR_TOP)}},\n};', '{SIDES(T_DOOR_TOP)}},\n' + BLOCK_ROWS + '};')
        # Logs need an axe; cobble is loose stone a hand can collect; gravel
        # digs slowly by hand; iron ore needs bronze, gold and diamond iron.
        one('0, 35,  TOOL_AXE,   0, B_LOG,', '0, 35,  TOOL_AXE,   1, B_LOG,')
        one('0, 40,  TOOL_PICK,  1, B_COBBLE,      {SIDES(T_COBBLE)}',
            '0, 40,  TOOL_PICK,  0, B_COBBLE,      {SIDES(T_COBBLE)}')
        one('0, 12,  TOOL_SHOVEL,0, B_GRAVEL,', '0, 40,  TOOL_SHOVEL,0, B_GRAVEL,')
        one('0, 50,  TOOL_PICK,  2, B_IRON_ORE,', '0, 50,  TOOL_PICK,  3, B_IRON_ORE,')
        one('0, 55,  TOOL_PICK,  3, B_GOLD_ORE,', '0, 55,  TOOL_PICK,  4, B_GOLD_ORE,')
        one('0, 60,  TOOL_PICK,  3, I_DIAMOND,', '0, 60,  TOOL_PICK,  4, I_DIAMOND,')
        defs = ''.join(f'\tdef_item({i},"{n}",{icon},TOOL_NONE,0,1,0,64,0);\n' for i, n, icon in MATERIALS)
        defs += ''.join(f'\tdef_item({i},"{n}",{icon},{tool},{tier},{dmg},0,1,0);\n'
                        f'\tg_itemDef[{i}].durability={uses};\n'
                        for i, n, icon, tool, tier, dmg, uses in NEW_TOOLS)
        defs += '\tdef_item(I_BELLOWS,"Bellows",T_I_BELLOWS,TOOL_NONE,0,1,0,1,0);\n'
        defs += ''.join(f'\tg_itemDef[{i}].tier={tier};\n' for i, tier in RETIER)
        defs += ('\tg_itemDef[B_FIRE].icon=T_I_FIRE;\n\tg_itemDef[B_FIRE].name="Campfire";\n'
                 '\tg_itemDef[B_FIRE_OUT].placeBlock=0;\n'
                 '\tg_itemDef[B_ANVIL].icon=T_ANVIL_TOP;\n'
                 '\tg_itemDef[B_CLAY_FURNACE].icon=T_CLAYF_FRONT;\n'
                 '\tg_itemDef[B_BLOOMERY].icon=T_BLOOMERY_FRONT;\n')
        one('\tg_itemDef[I_DIAMOND_SWORD].durability=1500;\n',
            '\tg_itemDef[I_DIAMOND_SWORD].durability=1500;\n' + defs)
    elif name == 'world.c':
        one('\t/* WALLTORCH DOORL_OPEN DOORU_OPEN */\n\t0,0,0,\n',
            '\t/* WALLTORCH DOORL_OPEN DOORU_OPEN */\n\t0,0,0,\n'
            '\t/* CLAY COPPER FIRE FIRE_OUT ANVIL CLAY_FURNACE BRICKS BLOOMERY */\n\t' +
            ','.join(map(str, NEW_OPACITY)) + ',\n')
    elif name == 'game.c':
        one('\t\tstatic const u8 mult[5]={1,2,4,6,9};\n\t\treturn mult[MIN(4,g_itemDef[item].tier)];',
            '\t\tstatic const u8 mult[6]={1,2,4,5,6,9};\n\t\treturn mult[MIN(5,g_itemDef[item].tier)];')
        one('\tif(TOOL_NONE!=tool && def->hardness>1) dos_tool_wear(tool==def->tool ? 1 : 2);\n',
            '\tif(TOOL_NONE!=tool && def->hardness>1) dos_tool_wear(tool==def->tool ? 1 : 2);\n'
            '\tdrop=dos_break_drop(id,b,drop);\n')
        one('\t/* A few starting items so the first night is survivable */\n\tinv_add(B_TORCH,4);\n',
            '\t/* Players start with nothing (EARLY_GAME.md). */\n')
    elif name == 'render.c':
        one('\tcase B_BED_FOOT:\n\tcase B_BED_HEAD:\n\t\t{\n\t\t\tstatic const u8 lo[3]={0,0,0},hi[3]={16,9,16};',
            '\tcase B_FIRE:\n\tcase B_FIRE_OUT:\n\t\t{\n'
            '\t\t\t/* A low ring of logs, with flames while lit. */\n'
            '\t\t\tstatic const u8 lo[3]={2,0,2},hi[3]={14,4,14};\n'
            '\t\t\tdraw_model_box(bx,by,bz,lo,hi,g_blockDef[id].tex,L,0);\n'
            '\t\t\tif(B_FIRE==id) draw_cross(bx,by,bz,T_FIRE_FLAME,15);\n'
            '\t\t}\n\t\tbreak;\n'
            '\tcase B_BED_FOOT:\n\tcase B_BED_HEAD:\n\t\t{\n\t\t\tstatic const u8 lo[3]={0,0,0},hi[3]={16,9,16};')


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
    materials(name, one)
    return text.encode(), recipe
