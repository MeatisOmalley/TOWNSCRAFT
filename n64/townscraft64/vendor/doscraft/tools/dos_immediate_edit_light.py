"""Keep player edit lighting synchronous during streamed-column work."""

from build_imported import replace_once


def adapt(name, data):
    if name != 'world.c':
        return data, []
    recipe = []
    text = data.decode('utf-8')
    text = replace_once(text,
        '\tu32 i;\n'
        '\tint deferLight;\n'
        '\tif(!in_world(x,y,z))\n',
        '\tu32 i;\n'
        '\tif(!in_world(x,y,z))\n', recipe)
    text = replace_once(text,
        '\tdeferLight=cacheAllocated && cache_light_pending();\n'
        '\tif(deferLight && !pending_light_edit_can_queue(x,y,z))\n'
        '\t{\n'
        '\t\t/* The bounded queue is only a safety valve for a burst of edits. */\n'
        '\t\tcache_light_pump(0);\n'
        '\t\tdeferLight=cache_light_pending();\n'
        '\t}\n',
        '\t/* Editing is rare: finish the shared stream-light queue before\n'
        '\t   changing a block, so its new light is ready for this frame. */\n'
        '\tif(cacheAllocated && cache_light_pending()) cache_light_pump(0);\n', recipe)
    text = replace_once(text,
        '\tif(deferLight) pending_light_edit_queue(x,y,z,b);\n'
        '\telse relight_edit(x,y,z,b);\n',
        '\trelight_edit(x,y,z,b);\n', recipe)
    return text.encode('utf-8'), recipe
