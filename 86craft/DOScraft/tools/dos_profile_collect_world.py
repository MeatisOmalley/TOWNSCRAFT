"""Benchmark-only timing overlay; production Towns source remains untouched."""

from build_imported import replace_once, lf_text


def adapt(name, data):
    if name not in ('render.c', 'game.c', 'world.c', 'column_cache.inc'):
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    if name != 'column_cache.inc':
        text = replace_once(text, '#include "bench.h"\n',
            '#include "bench.h"\n#include "bench_profile_cw.h"\n', recipe)
    if name == 'render.c':
        text = replace_once(text, '\tsetup_camera();\n',
            '\t{ u32 cwStart=bench_us(); setup_camera(); bench_cw_time(CW_CAMERA,bench_us()-cwStart); }\n', recipe)
        text = replace_once(text, '\t\t\tfloorOcclusion=find_floor_occlusion();\n',
            '\t\t\t{ u32 cwStart=bench_us(); floorOcclusion=find_floor_occlusion();\n'
            '\t\t\t  bench_cw_time(CW_FLOOR,bench_us()-cwStart); }\n', recipe)
        before = ('\t\tif(camBX!=cBX || camBY!=cBY || camBZ!=cBZ || g_meshVersion!=cMesh || g_renderScale!=cScale ||\n'
                  '\t\t   g_viewDist!=cView || floorOcclusion!=cFloor || dyaw>8 || ABS(g_cam.pitch-cPitch)>8)\n')
        after = ('\t\tunsigned cwReason=(camBX!=cBX ? 1u<<CW_X : 0) |\n'
                 '\t\t   (camBY!=cBY ? 1u<<CW_Y : 0) | (camBZ!=cBZ ? 1u<<CW_Z : 0) |\n'
                 '\t\t   (g_meshVersion!=cMesh ? 1u<<CW_MESH : 0) |\n'
                 '\t\t   (g_renderScale!=cScale ? 1u<<CW_SCALE : 0) |\n'
                 '\t\t   (g_viewDist!=cView ? 1u<<CW_VIEW : 0) |\n'
                 '\t\t   (floorOcclusion!=cFloor ? 1u<<CW_OCCLUSION : 0) |\n'
                 '\t\t   (dyaw>8 ? 1u<<CW_YAW : 0) |\n'
                 '\t\t   (ABS(g_cam.pitch-cPitch)>8 ? 1u<<CW_PITCH : 0);\n'
                 '\t\tbench_cw_reason(cwReason);\n'
                 '\t\tif(cwReason)\n')
        text = replace_once(text, before, after, recipe)
        text = replace_once(text, '\t\t\tu32 t=g_ticks;\n',
            '\t\t\tu32 t=g_ticks,cwStart;\n', recipe)
        text = replace_once(text, '\t\t\tcollect();\n\t\t\tsort_items();\n',
            '\t\t\tcwStart=bench_us(); collect(); bench_cw_time(CW_COLLECT,bench_us()-cwStart);\n'
            '\t\t\tcwStart=bench_us(); sort_items(); bench_cw_time(CW_SORT,bench_us()-cwStart);\n', recipe)
    elif name == 'game.c':
        text = replace_once(text,
            '\t\t\tint meshWorked=world_update_dirty_chunks(1);\n'
            '\t\t\tstream_world(meshWorked ? 0 : 1);\n',
            '\t\t\tu32 cwStart=bench_us();\n'
            '\t\t\tint meshWorked=world_update_dirty_chunks(1);\n'
            '\t\t\tbench_cw_time(CW_DIRTY,bench_us()-cwStart);\n'
            '\t\t\tcwStart=bench_us(); stream_world(meshWorked ? 0 : 1);\n'
            '\t\t\tbench_cw_time(CW_STREAM,bench_us()-cwStart);\n', recipe)
    elif name == 'world.c':
        text = replace_once(text,
            '\tscan_chunk_mask(cx,cy,cz,layers);\n'
            '\tpublish_chunk_layers(cx,cy,cz,layers);\n',
            '\t{ u32 cwStart=bench_us(); scan_chunk_mask(cx,cy,cz,layers);\n'
            '\t  bench_cw_time(CW_MESH_SCAN,bench_us()-cwStart);\n'
            '\t  cwStart=bench_us(); publish_chunk_layers(cx,cy,cz,layers);\n'
            '\t  bench_cw_time(CW_MESH_PUBLISH,bench_us()-cwStart); }\n', recipe)
        text = replace_once(text,
            '\tscan_chunk_mask(cx,cy,cz,((1u<<end)-1)^((1u<<meshJobLayer)-1));\n',
            '\t{ u32 cwStart=bench_us(); scan_chunk_mask(cx,cy,cz,((1u<<end)-1)^((1u<<meshJobLayer)-1));\n'
            '\t  bench_cw_time(CW_MESH_SCAN,bench_us()-cwStart); }\n', recipe)
        text = replace_once(text,
            '\tpublish_chunk_layers(cx,cy,cz,0xFFFF);\n',
            '\t{ u32 cwStart=bench_us(); publish_chunk_layers(cx,cy,cz,0xFFFF);\n'
            '\t  bench_cw_time(CW_MESH_PUBLISH,bench_us()-cwStart); }\n', recipe)
        text = replace_once(text,
            '\tif(cacheAllocated) cache_stream(x,z,maxBuild>100 ? 100000 : 1);\n',
            '\tif(cacheAllocated) { u32 cwStart=bench_us();\n'
            '\t  cache_stream(x,z,maxBuild>100 ? 100000 : 1);\n'
            '\t  bench_cw_time(CW_CACHE_STREAM,bench_us()-cwStart); }\n', recipe)
    else:
        text = replace_once(text,
            '\t\t\tcache_init_sky(slot,budget ? 64 : CS*CS);\n',
            '\t\t\t{ u32 cwStart=bench_us(); cache_init_sky(slot,budget ? 64 : CS*CS);\n'
            '\t\t\t  bench_cw_time(CW_SKY_INIT,bench_us()-cwStart); }\n', recipe)
        cached=' && !cacheLightCached' if 'budget && !cacheLightChannel && !cacheLightCached' in text else ''
        seed='cache_seed_light(cx,cz,cacheLightChannel,budget && !cacheLightChannel'+cached+' ? 64 : (CS+2)*(CS+2));'
        text = replace_once(text,
            '\t\t\t'+seed+'\n',
            '\t\t\t{ u32 cwStart=bench_us();\n'
            '\t\t\t  '+seed+'\n'
            '\t\t\t  bench_cw_time(CW_LIGHT_SEED,bench_us()-cwStart); }\n', recipe)
        text = replace_once(text,
            '\t\tlightBudget=budget; propagate(cacheLightChannel); lightBudget=0;\n',
            '\t\tlightBudget=budget;\n'
            '\t\t{ u32 cwStart=bench_us(); propagate(cacheLightChannel);\n'
            '\t\t  bench_cw_time(CW_LIGHT_FLOOD,bench_us()-cwStart); }\n'
            '\t\tlightBudget=0;\n', recipe)
        text = replace_once(text,
            '\tcache_light_pump(2048);\n',
            '\tcw_light_pump(2048);\n', recipe)
        text = replace_once(text,
            '\t\tcache_light_pump(budget>100 ? 0 : 2048);\n',
            '\t\tcw_light_pump(budget>100 ? 0 : 2048);\n', recipe)
        text = replace_once(text,
            'static int cache_load(int cx,int cz,int slot)\n',
            'static void cw_light_pump(int budget)\n'
            '{ u32 cwStart=bench_us(); cache_light_pump(budget);\n'
            '  bench_cw_time(CW_LIGHT,bench_us()-cwStart); }\n\n'
            'static int cache_load(int cx,int cz,int slot)\n', recipe)
        text = replace_once(text,
            'static void cache_stream(int x,int z,int budget)\n',
            'static int cw_column_prepare(int cx,int cz)\n'
            '{ u32 cwStart=bench_us(); int ok=column_prepare(cx,cz);\n'
            '  bench_cw_time(CW_COLUMN_PREPARE,bench_us()-cwStart); return ok; }\n'
            'static int cw_cache_load(int cx,int cz,int slot)\n'
            '{ u32 cwStart=bench_us(); int ok=cache_load(cx,cz,slot);\n'
            '  bench_cw_time(CW_COLUMN_LOAD,bench_us()-cwStart); return ok; }\n'
            'static int cw_finish_write(void)\n'
            '{ u32 cwStart=bench_us(); int ok=terrain_finish_write();\n'
            '  bench_cw_time(CW_WRITE_POLL,bench_us()-cwStart); return ok; }\n\n'
            'static void cache_stream(int x,int z,int budget)\n', recipe)
        text = replace_once(text,
            '\tif(terrainActive && terrainWriteID>=0 && terrain_finish_write()<=0) return;\n',
            '\tif(terrainActive && terrainWriteID>=0 && cw_finish_write()<=0) return;\n', recipe)
        text = replace_once(text,
            '\t\tif(slot<0 || !column_prepare(bx,bz) || !cache_load(bx,bz,slot)) return;\n',
            '\t\tif(slot<0 || !cw_column_prepare(bx,bz) || !cw_cache_load(bx,bz,slot)) return;\n', recipe)
    return text.encode('utf-8'), recipe
