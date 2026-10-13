"""Baked-light restore, conservative edit invalidation, and bounded RAM reuse."""
from build_imported import replace_once
def adapt(name,data):
    if name not in ('world.c','column_hdd.inc','column_cache.inc','column_store.inc','world.h'): return data,[]
    text=data.decode(); recipe=[]
    if '\r\n' in text: text=replace_once(text,'\r\n','\n',recipe)
    def one(before,after):
        nonlocal text
        text=replace_once(text,before,after,recipe)
    if name=='world.c':
        one('#include "hdd.h"','#include "hdd.h"\n#include "stream_light_state.h"')
        one('#include "large_world.inc"','#include "large_world.inc"\n#include "stream_light_cache.inc"')
        one('\tg_blocks[i]=b;\n\tif(terrainActive', '\tstream_light_invalidate(x,z);\n\tg_blocks[i]=b;\n\tif(terrainActive')
    elif name=='world.h':
        one('extern u32 g_terrainDiskStats[4];','extern u32 g_streamLightStats[4];\nextern u32 g_terrainDiskStats[4];')
    elif name=='column_store.inc':
        one('static int column_prepare(int cx,int cz)\n{',
            'static int column_prepare(int cx,int cz)\n{\n'
            '\t/* Already validated scratch is immutable while a separate light\n'
            '\t   buffer is being filled. Avoid hashing 12 KB again per poll. */\n'
            '\tif(preparedColumn==cz*g_NC+cx && streamLightRead==preparedColumn) return 1;')
    elif name=='column_hdd.inc':
        one('\t\tfor(int sector=0; sector<32; sector+=24)', '\t\tstream_light_header(header);\n\t\tfor(int sector=0; sector<32; sector+=24)')
        one('static void terrain_cache_allocate(void)\n{', 'static void terrain_cache_allocate(void)\n{\n\tstream_light_allocate();')
        one('\tterrainClock=0;', '\tterrainClock=0;\n\tstream_light_reset();')
        one('\tconst ColumnRecord *r=record;', '\tif(stream_light_quiesce()<=0) return streamError ? -1 : 0;\n\tconst ColumnRecord *r=record;')
        one('\tterrainBlocking=1;\n\tif(terrainFloppyReadID', '\tterrainBlocking=1;\n\tif(stream_light_quiesce()<0) { terrainBlocking=0; return 0; }\n\tif(terrainFloppyReadID')
        one('\tif(terrainReadID>=0) return 0;', '\tif(terrainReadID>=0 || streamLightRead>=0) return 0;')
    else:
        one('\t/* Validate before releasing the old owner; a bad record retains it. */',
            '\tif(!stream_light_prepare(cx,cz)) return 0;\n'
            '\t/* Validate before releasing the old owner; a bad record retains it. */')
        one('\tif(cacheBoot) { cache_init_sky(slot,CS*CS); return 1; }', '\tint bakedLight=stream_light_restore(cx,cz,slot);\n\tif(cacheBoot) { if(!bakedLight) cache_init_sky(slot,CS*CS); return 1; }')
        one('\tcacheLightSlot=slot; cacheLightChannel=0;', '''\t/* Clean bake columns share the whole-world light solution already. */
\tif(bakedLight) {
\t\tresidentReady[slot]=1;
\t\tcache_dirty_seam(cx-1,cz); cache_dirty_seam(cx+1,cz);
\t\tcache_dirty_seam(cx,cz-1); cache_dirty_seam(cx,cz+1);
\t\treturn 1;
\t}
\tcacheLightSlot=slot; cacheLightChannel=0;''')
    return text.encode(),recipe
