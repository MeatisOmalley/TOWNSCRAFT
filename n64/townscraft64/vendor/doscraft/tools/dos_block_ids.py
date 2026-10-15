"""DOS 6-bit block IDs: 64 block kinds, 2 metadata bits, items from 64.

A world cell stays one byte. The Towns layout was a 5-bit ID and 3 bits of
metadata; this moves one bit to the ID. Every existing block keeps its ID and
baked terrain stays byte-identical (it never sets bit 5, and the baked leaf
rim flag at bit 6 stays a metadata bit).

Only torches (floor plus four walls) and doors (side plus open) needed the
third metadata bit. Wall torches become their own block with the facing in
metadata, and open doors become their own blocks. Item IDs move from 32 to
64 so block IDs 32-63 are free. Saves written before this change store the
old item IDs and must not be loaded.
"""
from build_imported import replace_once

NAMES = ('blocks.h', 'blocks.c', 'world.h', 'world.c', 'game.c', 'render.c')


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

    if name == 'blocks.h':
        one('/* A world cell is one byte: block id in bits 0-4, metadata in bits 5-7. */\n'
            '#define BLK_ID(b)   ((b)&31)\n'
            '#define BLK_META(b) ((b)>>5)\n'
            '#define MKBLK(id,meta) ((u8)((id)|((meta)<<5)))',
            '/* A world cell is one byte: block id in bits 0-5, metadata in bits 6-7. */\n'
            '#define BLOCK_IDS   64\n'
            '#define BLK_ID(b)   ((b)&63)\n'
            '#define BLK_META(b) ((b)>>6)\n'
            '#define MKBLK(id,meta) ((u8)((id)|((meta)<<6)))')
        one('\tB_TALLGRASS,B_STONEBRICK,B_TNT,B_CHEST,B_GOLD_ORE,B_DIAMOND_ORE,\n'
            '\tNUM_BLOCKS\n};\n',
            '\tB_TALLGRASS,B_STONEBRICK,B_TNT,B_CHEST,B_GOLD_ORE,B_DIAMOND_ORE,\n'
            '\t/* Split out when metadata shrank to two bits */\n'
            '\tB_WALL_TORCH,B_DOOR_LOWER_OPEN,B_DOOR_UPPER_OPEN,\n'
            '\tNUM_BLOCKS\n};\n\n'
            '/* Wall torch metadata is the wall it hangs on (0 -X, 1 +X, 2 -Z, 3 +Z).\n'
            '   Door metadata is the side of the cell (0-3); an open door is its own\n'
            '   block. DOOR_META rebuilds the old three-bit form (bit 2 = open). */\n'
            'static inline int door_is_lower(int id){return B_DOOR_LOWER==id || B_DOOR_LOWER_OPEN==id;}\n'
            'static inline int door_is_upper(int id){return B_DOOR_UPPER==id || B_DOOR_UPPER_OPEN==id;}\n'
            '#define DOOR_META(b) (BLK_META(b)|((B_DOOR_LOWER_OPEN==BLK_ID(b) || B_DOOR_UPPER_OPEN==BLK_ID(b)) ? 4 : 0))\n'
            '#define DOOR_BLK(lower,meta) MKBLK(((meta)&4) ? ((lower) ? B_DOOR_LOWER_OPEN : B_DOOR_UPPER_OPEN) : \\\n'
            '\t((lower) ? B_DOOR_LOWER : B_DOOR_UPPER),(meta)&3)\n')
        one('/* Items share the id space.  Ids below 32 are the block of the same id. */',
            '/* Items share the id space.  Ids below BLOCK_IDS are the block of the same id. */')
        one('\tI_STICK=32,', '\tI_STICK=BLOCK_IDS,')
        one('extern const BlockDef g_blockDef[NUM_BLOCKS];',
            'extern const BlockDef g_blockDef[BLOCK_IDS];')
    elif name == 'blocks.c':
        # Sized for every ID a cell can hold: mesh tables look up all 256 bytes.
        one('const BlockDef g_blockDef[NUM_BLOCKS]=', 'const BlockDef g_blockDef[BLOCK_IDS]=')
        one('{SIDES(T_DIAMOND_ORE)}},\n};',
            '{SIDES(T_DIAMOND_ORE)}},\n'
            '\t{"Torch",      BF_MODEL,                          14,  1,  TOOL_NONE,  0, B_TORCH,       {SIDES(T_TORCH)}},\n'
            '\t{"Door",       BF_MODEL|BF_SOLID|BF_USABLE,        0, 30,  TOOL_AXE,   0, I_DOOR,        {SIDES(T_DOOR_BOTTOM)}},\n'
            '\t{"Door",       BF_MODEL|BF_SOLID|BF_USABLE,        0, 30,  TOOL_AXE,   0, I_DOOR,        {SIDES(T_DOOR_TOP)}},\n'
            '};')
        one('\tg_itemDef[B_DOOR_UPPER].placeBlock=0;\n',
            '\tg_itemDef[B_DOOR_UPPER].placeBlock=0;\n'
            '\tg_itemDef[B_WALL_TORCH].placeBlock=0;\n'
            '\tg_itemDef[B_DOOR_LOWER_OPEN].placeBlock=0;\n'
            '\tg_itemDef[B_DOOR_UPPER_OPEN].placeBlock=0;\n')
        one('\tint meta=BLK_META(b),side=meta&3;', '\tint meta=DOOR_META(b),side=meta&3;')
        one('\tcase B_DOOR_LOWER:\n\tcase B_DOOR_UPPER:\n\t\tfor(k=0; k<3; ++k)',
            '\tcase B_DOOR_LOWER:\n\tcase B_DOOR_UPPER:\n'
            '\tcase B_DOOR_LOWER_OPEN:\n\tcase B_DOOR_UPPER_OPEN:\n\t\tfor(k=0; k<3; ++k)')
    elif name == 'world.h':
        one('extern const u8 g_lightOpacity[NUM_BLOCKS];', 'extern const u8 g_lightOpacity[BLOCK_IDS];')
    elif name == 'world.c':
        one('const u8 g_lightOpacity[NUM_BLOCKS]=', 'const u8 g_lightOpacity[BLOCK_IDS]=')
        one('\t/* TALLGRASS STONEBRICK TNT CHEST GOLD DIAMOND */\n\t0,15,15,15,15,15,\n',
            '\t/* TALLGRASS STONEBRICK TNT CHEST GOLD DIAMOND */\n\t0,15,15,15,15,15,\n'
            '\t/* WALLTORCH DOORL_OPEN DOORU_OPEN */\n\t0,0,0,\n')
    elif name == 'game.c':
        one('\telse if(B_DOOR_LOWER==id)\n', '\telse if(door_is_lower(id))\n')
        one('\t/* A wall torch with meta m hangs on the wall at -X,+X,-Z,+Z of its cell */',
            '\t/* A wall torch with meta k hangs on the wall at -X,+X,-Z,+Z of its cell */')
        one('\t\tif(B_TORCH==BLK_ID(b) && BLK_META(b)==k+1)',
            '\t\tif(B_WALL_TORCH==BLK_ID(b) && BLK_META(b)==k)')
        one('\tcase B_DOOR_LOWER:\n\t\tworld_set(x,y+1,z,B_AIR);\n\t\tbreak;\n'
            '\tcase B_DOOR_UPPER:\n\t\tworld_set(x,y-1,z,B_AIR);\n',
            '\tcase B_DOOR_LOWER:\n\tcase B_DOOR_LOWER_OPEN:\n\t\tworld_set(x,y+1,z,B_AIR);\n\t\tbreak;\n'
            '\tcase B_DOOR_UPPER:\n\tcase B_DOOR_UPPER_OPEN:\n\t\tworld_set(x,y-1,z,B_AIR);\n')
        one('\tcase B_DOOR_LOWER:\n\tcase B_DOOR_UPPER:\n\t\t{\n'
            '\t\t\tint ly=(B_DOOR_LOWER==id) ? tY : tY-1;\n'
            '\t\t\tu8 lo=wget(tX,ly,tZ),hi=wget(tX,ly+1,tZ);\n'
            '\t\t\tint meta=BLK_META(lo)^4;\n'
            '\t\t\tworld_set(tX,ly,tZ,MKBLK(BLK_ID(lo),meta));',
            '\tcase B_DOOR_LOWER:\n\tcase B_DOOR_UPPER:\n'
            '\tcase B_DOOR_LOWER_OPEN:\n\tcase B_DOOR_UPPER_OPEN:\n\t\t{\n'
            '\t\t\tint ly=door_is_lower(id) ? tY : tY-1;\n'
            '\t\t\tu8 lo=wget(tX,ly,tZ),hi=wget(tX,ly+1,tZ);\n'
            '\t\t\tint meta=DOOR_META(lo)^4;\n'
            '\t\t\tworld_set(tX,ly,tZ,DOOR_BLK(1,meta));')
        one('\t\t\tif(B_DOOR_UPPER==BLK_ID(hi))\n\t\t\t{\n'
            '\t\t\t\tworld_set(tX,ly+1,tZ,MKBLK(B_DOOR_UPPER,meta));',
            '\t\t\tif(door_is_upper(BLK_ID(hi)))\n\t\t\t{\n'
            '\t\t\t\tworld_set(tX,ly+1,tZ,DOOR_BLK(0,meta));')
        one('\t\tworld_set(px,py,pz,MKBLK(B_TORCH,meta));',
            '\t\tworld_set(px,py,pz,meta ? MKBLK(B_WALL_TORCH,meta-1) : MKBLK(B_TORCH,0));')
        one('MKBLK(B_DOOR_LOWER,2|TEST_DOOR_OPEN)', 'DOOR_BLK(1,2|TEST_DOOR_OPEN)')
        one('MKBLK(B_DOOR_UPPER,2|TEST_DOOR_OPEN)', 'DOOR_BLK(0,2|TEST_DOOR_OPEN)')
        one('MKBLK(B_TORCH,2)', 'MKBLK(B_WALL_TORCH,1)')
    elif name == 'render.c':
        one('\tcase B_TORCH:\n\t\t{\n\t\t\tstatic const u8 floorLo',
            '\tcase B_WALL_TORCH:\n'
            '\t\tmeta+=1; /* The model code numbers wall facings 1-4 */\n'
            '\t\t/* fall through */\n'
            '\tcase B_TORCH:\n\t\t{\n\t\t\tstatic const u8 floorLo')
        one('\tcase B_DOOR_LOWER:\n\tcase B_DOOR_UPPER:\n\t\t{\n'
            '\t\t\tu8 tex[6],lo[3],hi[3];\n'
            '\t\t\tint k,t=(id==B_DOOR_LOWER ? T_DOOR_BOTTOM : T_DOOR_TOP);',
            '\tcase B_DOOR_LOWER:\n\tcase B_DOOR_UPPER:\n'
            '\tcase B_DOOR_LOWER_OPEN:\n\tcase B_DOOR_UPPER_OPEN:\n\t\t{\n'
            '\t\t\tu8 tex[6],lo[3],hi[3];\n'
            '\t\t\tint k,t=(door_is_lower(id) ? T_DOOR_BOTTOM : T_DOOR_TOP);')
    return text.encode(), recipe
