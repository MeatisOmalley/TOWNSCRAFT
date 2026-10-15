"""Only sound event selection changes; no game rules or UI changes."""
from build_imported import replace_once


def adapt(name,data):
    if name!='game.c':return data,[]
    text=data.decode();recipe=[]
    if '\r\n' in text:text=replace_once(text,'\r\n','\n',recipe)
    text=replace_once(text,'#include "sound.h"\n','#include "sound.h"\n#include "game_audio.h"\n',recipe)
    text=replace_once(text,'pitch=pitch*3/2;', 'id=DOS_SFX_GLASS;',recipe)
    text=replace_once(text,'\tsound_play_at(id,pitch,vol,x*FU+FU/2,y*FU+FU/2,z*FU+FU/2);',
        '\tif(BLK_ID(b)==B_SAND)id=DOS_SFX_SAND;\n'
        '\telse if(BLK_ID(b)==B_GRAVEL)id=DOS_SFX_GRAVEL;\n'
        '\tsound_play_at(id,pitch,vol,x*FU+FU/2,y*FU+FU/2,z*FU+FU/2);',recipe)
    text=replace_once(text,'\t\t\t\tint tool=g_blockDef[BLK_ID(wget(bx,by,bz))].tool;\n', '',recipe)
    text=replace_once(text,'sound_play(SFX_STEP,(TOOL_PICK==tool) ? 300 : (TOOL_AXE==tool ? 250 : 210),200,0);',
        'dos_sound_step(BLK_ID(wget(bx,by,bz)),200);',recipe)
    return text.encode(),recipe
