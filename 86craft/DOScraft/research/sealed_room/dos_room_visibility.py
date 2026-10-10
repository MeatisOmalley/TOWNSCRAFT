"""Rejected, unintegrated draft. Not a build option; see README.md."""
from build_imported import replace_once

FRONTEND=r'''
static DosRoomProof roomProof;
/* Never infer sealing from wget's unloaded/out-of-world bedrock sentinel.
   Require published, untrimmed meshes even for passable cells. */
static int room_read_cell(int x,int y,int z)
{
    Chunk *ch;
    int flags;
    if(!in_world(x,y,z) || !world_column_sim_ready(x/CS,z/CS)) return -1;
    ch=&g_chunks[chunk_index(x/CS,y/CS,z/CS)];
    if(!ch->meshed || ch->dirty || ch->dirtyLayers || ch->trimmed) return -1;
    flags=blk_flags(wget(x,y,z));
    return (flags&(BF_CUBE|BF_OPAQUE))==(BF_CUBE|BF_OPAQUE) && !(flags&BF_MODEL);
}
static int room_ready(void)
{
    int x,y,z;
    if(!roomProof.valid) return 1;
    for(z=roomProof.lo[2]/CS;z<=(roomProof.hi[2]-1)/CS;++z)
    for(x=roomProof.lo[0]/CS;x<=(roomProof.hi[0]-1)/CS;++x) {
        if(!world_column_sim_ready(x,z)) return 0;
        for(y=roomProof.lo[1]/CS;y<=(roomProof.hi[1]-1)/CS;++y) {
            Chunk *ch=&g_chunks[chunk_index(x,y,z)];
            if(!ch->meshed || ch->dirty || ch->dirtyLayers || ch->trimmed) return 0;
        }
    }
    return 1;
}
static int room_margin(void)
{
    /* Deliberately conservative near-plane guard, regardless of rotation.
       A camera close to ANY cell plane uses the original renderer. */
    int x=g_cam.x&255,y=g_cam.y&255,z=g_cam.z&255;
    return x>=32 && x<=224 && y>=32 && y<=224 && z>=32 && z<=224;
}
static void room_prepare(void)
{
    roomProof.valid=0;
    if(!room_margin() || !in_world(camBX,camBY,camBZ) ||
       !world_column_sim_ready(camBX/CS,camBZ/CS)) return;
    /* Above the highest light-blocking block there cannot be an enclosure. */
    if(camBY>=world_height(camBX,camBZ)) return;
    dos_room_prove(&roomProof,camBX,camBY,camBZ,room_read_cell);
}
'''

def adapt(name,data):
    if name!='render.c': return data,[]
    recipe=[]; text=data.decode('utf-8')
    if '\r\n' in text:
        recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
        text=text.replace('\r\n','\n')
    text=replace_once(text,'#include "gfx.h"\n','#include "gfx.h"\n#include "room_visibility.h"\n',recipe)
    text=replace_once(text,'static int skyDarkenCur;\n','static int skyDarkenCur;\n'+FRONTEND,recipe)
    text=replace_once(text,'void render_init(void)\n{\n\tint t,i;\n',
        'void render_init(void)\n{\n\tint t,i;\n\troomProof.valid=0;\n',recipe)
    text=replace_once(text,'static void collect(void)\n{\n',
        'static void collect(void)\n{\n\troom_prepare();\n',recipe)
    text=replace_once(text,'\t\t\t\t\tby0=y0+(box[2]&15); by1=y0+(box[2]>>4)+1;\n',
        '\t\t\t\t\tby0=y0+(box[2]&15); by1=y0+(box[2]>>4)+1;\n'
        '\t\t\t\t\tif(dos_room_outside(&roomProof,bx0,bx1,by0,by1,bz0,bz1)) { q=gend; continue; }\n',recipe)
    text=replace_once(text,'\t\t\t\t\t\tez=(dir<=DIR_PY) ? MQ_H(w0) : 1;\n',
        '\t\t\t\t\t\tez=(dir<=DIR_PY) ? MQ_H(w0) : 1;\n'
        '\t\t\t\t\t\tif(dos_room_outside(&roomProof,x,x+ex,y,y+1,z,z+ez)) continue;\n',recipe)
    text=replace_once(text,'\t\tif(camBX!=cBX || camBY!=cBY || camBZ!=cBZ || g_meshVersion!=cMesh || g_renderScale!=cScale ||\n',
        '\t\tif((roomProof.valid && (!room_ready() || !room_margin())) ||\n'
        '\t\t   camBX!=cBX || camBY!=cBY || camBZ!=cBZ || g_meshVersion!=cMesh || g_renderScale!=cScale ||\n',recipe)
    return text.encode('utf-8'),recipe
