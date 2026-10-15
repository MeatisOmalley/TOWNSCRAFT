"""Install baked column light without clearing it first.

cache_load() zeroed the slot's 12 KiB light buffer and then let
stream_light_restore() overwrite all of it when the baked/cached light was
valid. The clear now runs only when no baked light was restored; every
failure path of stream_light_restore() returns before writing the buffer.
"""
from build_imported import replace_once, lf_text


def adapt(name, data):
    if name != 'column_cache.inc':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    text = replace_once(text,
        '\tmemset(g_light+slot*COLUMN_CELLS,0,COLUMN_CELLS);\n'
        '\tcacheSkyCell=0; cacheLightSeed=0;\n'
        '\tint bakedLight=stream_light_restore(cx,cz,slot);\n',
        '\tcacheSkyCell=0; cacheLightSeed=0;\n'
        '\t/* A restored bake overwrites the whole slot; clear only otherwise. */\n'
        '\tint bakedLight=stream_light_restore(cx,cz,slot);\n'
        '\tif(!bakedLight) memset(g_light+slot*COLUMN_CELLS,0,COLUMN_CELLS);\n', recipe)
    return text.encode('utf-8'), recipe
