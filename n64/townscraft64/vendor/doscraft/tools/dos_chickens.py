"""DOS chicken mob, appended save IDs, model, food and texture generation."""
from build_imported import replace_once


def adapt(name,data):
    if name not in ('mobs.h','mobs.c','blocks.h','blocks.c','inventory.c','textures.c'):
        return data,[]
    text=data.decode();recipe=[]
    if '\r\n' in text:
        recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
        text=text.replace('\r\n','\n')
    def one(before,after):
        nonlocal text
        text=replace_once(text,before,after,recipe)
    if name=='mobs.h':
        one('MOB_CREEPER,MOB_TNT,','MOB_CREEPER,MOB_TNT,MOB_CHICKEN,')
    elif name=='blocks.h':
        one('\tNUM_ITEMS','\tI_RAW_CHICKEN,I_COOKED_CHICKEN,I_FEATHER,\n\tNUM_ITEMS')
        one('\t/* Item icons */','\tT_CHICKEN_SKIN,T_CHICKEN_FACE,T_CHICKEN_YELLOW,\n\t/* Item icons */')
        one('\t/* Misc */','\tT_I_CHICKEN,T_I_COOKED_CHICKEN,T_I_FEATHER,\n\t/* Misc */')
    elif name=='blocks.c':
        one('\tdef_item(I_GOLDEN_APPLE,"Golden Apple",  T_I_GOLDEN_APPLE,TOOL_NONE,  0,1,20,64,0);',
            '\tdef_item(I_GOLDEN_APPLE,"Golden Apple",  T_I_GOLDEN_APPLE,TOOL_NONE,  0,1,20,64,0);\n'
            '\tdef_item(I_RAW_CHICKEN,"Raw Chicken",T_I_CHICKEN,TOOL_NONE,0,1,2,64,0);\n'
            '\tdef_item(I_COOKED_CHICKEN,"Cooked Chicken",T_I_COOKED_CHICKEN,TOOL_NONE,0,1,6,64,0);\n'
            '\tdef_item(I_FEATHER,"Feather",T_I_FEATHER,TOOL_NONE,0,1,0,64,0);')
    elif name=='inventory.c':
        # Append to preserve the existing craft-grid recipe indices.
        one('\tR1(ST_FURNACE,I_COAL,1,B_LOG,1),',
            '\tR1(ST_FURNACE,I_COAL,1,B_LOG,1),\n\tR1(ST_FURNACE,I_COOKED_CHICKEN,1,I_RAW_CHICKEN,1),')
    elif name=='textures.c':
        one('\tbegin(T_PLAYER_SKIN); noisefill(R_PEACH,12,2);',
            '\tbegin(T_PLAYER_SKIN); noisefill(R_PEACH,12,2);\n'
            '\tbegin(T_CHICKEN_SKIN); base_specks(R_GRAY,15,14,8,15,0);\n'
            '\tbegin(T_CHICKEN_FACE); rect(0,0,15,15,P(R_GRAY,15));\n'
            '\trect(2,5,3,7,P(R_GRAY,1)); rect(12,5,13,7,P(R_GRAY,1));\n'
            '\trect(6,13,9,15,P(R_RED,15));\n'
            '\tbegin(T_CHICKEN_YELLOW); base_specks(R_FLAME,14,13,12,15,4);')
        one('\tbegin(T_CRACK);',
            '\tbegin(T_I_CHICKEN); gen_meat(R_PINK,14);\n'
            '\tbegin(T_I_COOKED_CHICKEN); gen_meat(R_PLANK,13);\n'
            '\tbegin(T_I_FEATHER); line(3,13,12,3,P(R_GRAY,14));\n'
            '\tfor(int y=3;y<12;++y) { int x=15-y; line(x-2,y,x,y,P(R_GRAY,15)); }\n'
            '\tbegin(T_CRACK);')
    else:
        one('\t{2000,4000,1,0},      /* TNT */',
            '\t{2000,4000,1,0},      /* TNT */\n\t{819,2867,4,260},      /* Chicken */')
        one('int is_hostile(int type)\n',
            '/* Seven boxes keep the existing 160-box budget even at full mob caps. */\n'
            'static const Part chickenParts[]=\n{\n'
            '\t{0,6,0, -3,3,-4,3,9,3, 0,T_CHICKEN_SKIN,T_CHICKEN_SKIN,T_CHICKEN_SKIN},\n'
            '\t{0,9,3, -2,7,2,2,12,5, 0,T_CHICKEN_SKIN,T_CHICKEN_FACE,T_CHICKEN_SKIN},\n'
            '\t{0,8,5, -2,7,5,2,9,7, 0,T_CHICKEN_YELLOW,T_CHICKEN_YELLOW,T_CHICKEN_YELLOW},\n'
            '\t{-2,3,0, -2,0,-1,-1,3,2, 1,T_CHICKEN_YELLOW,T_CHICKEN_YELLOW,T_CHICKEN_YELLOW},\n'
            '\t{2,3,0, 1,0,-1,2,3,2, 2,T_CHICKEN_YELLOW,T_CHICKEN_YELLOW,T_CHICKEN_YELLOW},\n'
            '\t{-3,8,0, -4,4,-3,-3,8,2, 4,T_CHICKEN_SKIN,T_CHICKEN_SKIN,T_CHICKEN_SKIN},\n'
            '\t{3,8,0, 3,4,-3,4,8,2, 5,T_CHICKEN_SKIN,T_CHICKEN_SKIN,T_CHICKEN_SKIN},\n'
            '};\n\nint is_hostile(int type)\n')
        one('\tcase MOB_SHEEP:\n\t\tinv_add(B_WOOL',
            '\tcase MOB_CHICKEN:\n\t\tinv_add(I_RAW_CHICKEN,1);\n'
            '\t\t{ int feathers=rnd_range(3); if(feathers) inv_add(I_FEATHER,feathers); }\n'
            '\t\tgame_message("+ Raw Chicken");\n\t\tbreak;\n'
            '\tcase MOB_SHEEP:\n\t\tinv_add(B_WOOL')
        one('hurtPitch[]={256,420,480,170,300,256};','hurtPitch[]={256,420,480,170,300,256,520};')
        one('\t\tbody_tick(&m->body,GRAVITY);\n\t\tm->body.fallDist=0;',
            '\t\t/* Cap descent before collision/sub-stepping; normal hops still work. */\n'
            '\t\tif(m->type==MOB_CHICKEN && !m->body.inWater && m->body.vy<-200) m->body.vy=-200;\n'
            '\t\tbody_tick(&m->body,GRAVITY);\n\t\tm->body.fallDist=0;')
        one('\t\tcase MOB_TNT:     parts=tntParts;',
            '\t\tcase MOB_CHICKEN: parts=chickenParts; nParts=ARRAY_LEN(chickenParts); break;\n'
            '\t\tcase MOB_TNT:     parts=tntParts;')
        one('\t\t\tcase 3: b->pitch=-240+legSwing/4; break;',
            '\t\t\tcase 3: b->pitch=-240+legSwing/4; break;\n'
            '\t\t\tcase 4: b->pitch=m->body.onGround ? 0 : 96+legSwing; break;\n'
            '\t\t\tcase 5: b->pitch=m->body.onGround ? 0 : -96-legSwing; break;')
        one('mob_spawn((r&2) ? MOB_PIG : MOB_SHEEP,',
            'mob_spawn((r>>1)%3==2 ? MOB_CHICKEN : (r>>1)%3==1 ? MOB_PIG : MOB_SHEEP,')
        one('(sleeping && type!=MOB_PIG && type!=MOB_SHEEP)',
            '(sleeping && type!=MOB_PIG && type!=MOB_SHEEP && type!=MOB_CHICKEN)')
    return text.encode(),recipe
