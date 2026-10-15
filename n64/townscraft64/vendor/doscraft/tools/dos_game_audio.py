"""Only sound event selection, mob calls and water sounds; no game rules or UI changes."""
from build_imported import replace_once,lf_text


def adapt(name,data):
    if name not in ('game.c','mobs.c'):return data,[]
    recipe=[]
    text=lf_text(data,recipe)
    if name=='mobs.c':
        # Idle calls (the zombie groan); their own random source, so mob behavior is unchanged.
        text=replace_once(text,'#include "sound.h"\n','#include "sound.h"\n#include "game_audio.h"\n',recipe)
        text=replace_once(text,'\t\tif(m->attackTimer>0) --m->attackTimer;\n',
            '\t\tif(m->attackTimer>0) --m->attackTimer;\n'
            '\t\tdos_sound_mob_tick(m->type,m->body.x,m->body.y+FU,m->body.z);\n',recipe)
        # Per-mob hurt and death cries; without them, the original pitched hurt sound.
        hurt=('\t\tsound_play_at(SFX_HURT,hurtPitch[m->type<ARRAY_LEN(hurtPitch) ? m->type : 0],220,'
              'm->body.x,m->body.y+FU,m->body.z);\n')
        text=replace_once(text,hurt,
            '\t\tif(!dos_sound_mob(m->type,m->health<=0,m->body.x,m->body.y+FU,m->body.z))\n\t'+hurt,recipe)
        return text.encode(),recipe
    text=replace_once(text,'#include "sound.h"\n','#include "sound.h"\n#include "game_audio.h"\n',recipe)
    # Music spacing as in Minecraft Java/Eaglercraft: 10-20 minutes between
    # songs in a world, the first one 15 s to 5 minutes in (new or loaded).
    gap=('\tmusic_set_gap(12000);    /* In game: 2-4 minutes of silence between plays */\n'
         '\tmusic_schedule(1500);\n')
    if text.count(gap)!=2:raise ValueError('Expected the two in-game music gap settings')
    spaced=('\tmusic_set_gap(60000);    /* In game: 10-20 minutes between songs, as in Minecraft */\n'
            '\tmusic_schedule(1500+(int)(((u32)g_ticks*2654435761u>>16)%28501u));   /* First: 15 s-5 min */\n')
    recipe.append(dict(operation='replace',before=gap,after=spaced,count=2))
    text=text.replace(gap,spaced)
    text=replace_once(text,'pitch=pitch*3/2;', 'id=DOS_SFX_GLASS;',recipe)
    text=replace_once(text,'\tsound_play_at(id,pitch,vol,x*FU+FU/2,y*FU+FU/2,z*FU+FU/2);',
        '\tif(BLK_ID(b)==B_SAND)id=DOS_SFX_SAND;\n'
        '\telse if(BLK_ID(b)==B_GRAVEL)id=DOS_SFX_GRAVEL;\n'
        '\tsound_play_at(id,pitch,vol,x*FU+FU/2,y*FU+FU/2,z*FU+FU/2);',recipe)
    # Minecraft mining hits: the block's footstep, quiet and an octave down.
    text=replace_once(text,'\t\t\t\tblock_sfx(b,tX,tY,tZ,110,330);   /* Hitting */\n',
        '\t\t\t\tif(!dos_sound_hit(b,tX,tY,tZ)) block_sfx(b,tX,tY,tZ,110,330);   /* Hitting */\n',recipe)
    # A splash on entering water and a stroke every 2 blocks swum; teleports
    # (respawn, loading) move a block or more in a tick and stay silent.
    text=replace_once(text,'\t\tstatic int lastX,lastZ,stepDist;\n','\t\tstatic int lastX,lastZ,stepDist,swimDist,wasInWater;\n',recipe)
    text=replace_once(text,'\t\tlastZ=b->z;\n\t\tif(b->onGround && !b->inWater && d<FU)\n',
        '\t\tlastZ=b->z;\n'
        '\t\tif(b->inWater && d<FU)\n'
        '\t\t{\n'
        '\t\t\tif(!wasInWater) dos_sound_splash(b->vy);\n'
        '\t\t\tswimDist+=d;\n'
        '\t\t\tif(swimDist>FU*2)\n'
        '\t\t\t{\n'
        '\t\t\t\tswimDist=0;\n'
        '\t\t\t\tdos_sound_swim();\n'
        '\t\t\t}\n'
        '\t\t}\n'
        '\t\twasInWater=b->inWater;\n'
        '\t\tif(b->onGround && !b->inWater && d<FU)\n',recipe)
    text=replace_once(text,'\t\t\t\tint tool=g_blockDef[BLK_ID(wget(bx,by,bz))].tool;\n', '',recipe)
    text=replace_once(text,'sound_play(SFX_STEP,(TOOL_PICK==tool) ? 300 : (TOOL_AXE==tool ? 250 : 210),200,0);',
        'dos_sound_step(BLK_ID(wget(bx,by,bz)),200);',recipe)
    return text.encode(),recipe
