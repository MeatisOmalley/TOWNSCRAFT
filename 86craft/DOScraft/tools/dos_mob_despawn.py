"""DOS-only bounded mob lifetime: distant animals are not kept dormant."""

from build_imported import replace_once, lf_text


def adapt(name, data):
    if name != 'mobs.c':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    text = replace_once(text,
        '\t/* Create the dormant owner before releasing a simulation slot. */\n'
        '\tfor(i=0; i<MAX_MOBS; ++i)\n'
        '\t{\n'
        '\t\tMob *m=&g_mobs[i];\n'
        '\t\tif(m->type==MOB_NONE || m->type==MOB_TNT || region_distance2(m)<=32*32) continue;\n'
        '\t\tif(is_hostile(m->type))\n'
        '\t\t{\n'
        '\t\t\tif(!m->fuse) m->type=MOB_NONE;\n'
        '\t\t}\n'
        '\t\telse if(dormantCount<MAX_DORMANT_MOBS)\n'
        '\t\t{\n'
        '\t\t\tdormant[dormantCount].mob=*m;\n'
        '\t\t\tdormant[dormantCount++].id=g_mobIDs[i];\n'
        '\t\t\tm->type=MOB_NONE;\n'
        '\t\t}\n'
        '\t}\n',
        '\t/* All animals and hostile mobs leave the simulation beyond 32 blocks.\n'
        '\t   Keep primed TNT until its fuse resolves. */\n'
        '\tfor(i=0; i<MAX_MOBS; ++i)\n'
        '\t{\n'
        '\t\tMob *m=&g_mobs[i];\n'
        '\t\tif(m->type!=MOB_NONE && m->type!=MOB_TNT &&\n'
        '\t\t   region_distance2(m)>32*32) m->type=MOB_NONE;\n'
        '\t}\n'
        '\t/* Old saves may contain dormant animals; they no longer respawn. */\n'
        '\tdormantCount=0;\n', recipe)
    text = replace_once(text,
        '\tdormantCount=nd;\n\treturn 1;\n',
        '\t/* Accept old saves, then discard formerly persistent distant mobs. */\n'
        '\tdormantCount=0;\n\treturn 1;\n', recipe)
    return text.encode('utf-8'), recipe
