"""Only sound event selection and the zombie groan; no game rules or UI changes."""
from build_imported import replace_once,lf_text


def adapt(name,data):
    if name not in ('game.c','mobs.c'):return data,[]
    recipe=[]
    text=lf_text(data,recipe)
    if name=='mobs.c':
        # Zombie ambient groan; its own random source, so mob behavior is unchanged.
        text=replace_once(text,'#include "sound.h"\n','#include "sound.h"\n#include "game_audio.h"\n',recipe)
        text=replace_once(text,'\t\tif(m->attackTimer>0) --m->attackTimer;\n',
            '\t\tif(m->attackTimer>0) --m->attackTimer;\n'
            '\t\tif(MOB_ZOMBIE==m->type) dos_sound_zombie(m->body.x,m->body.y+FU,m->body.z);\n',recipe)
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
    text=replace_once(text,'\t\t\t\tint tool=g_blockDef[BLK_ID(wget(bx,by,bz))].tool;\n', '',recipe)
    text=replace_once(text,'sound_play(SFX_STEP,(TOOL_PICK==tool) ? 300 : (TOOL_AXE==tool ? 250 : 210),200,0);',
        'dos_sound_step(BLK_ID(wget(bx,by,bz)),200);',recipe)
    return text.encode(),recipe
