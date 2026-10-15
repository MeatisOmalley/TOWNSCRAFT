"""A 20-minute day, as in Minecraft Java and Eaglercraft (24000 ticks at 20 Hz).

The Towns clock advanced g_time by 2 per 20 Hz game tick over its 0..23999
day, a 10-minute cycle. One step per tick keeps every time-of-day threshold
(sunset, night spawning, dawn) in the same proportion. Saves store g_time, so
existing worlds keep their time of day.
"""
from build_imported import replace_once, lf_text


def adapt(name, data):
    if name != 'game.c':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    text = replace_once(text, '\tg_time=(g_time+2)%24000;\n',
                        '\tg_time=(g_time+1)%24000;   /* 20-minute day, as in Minecraft */\n', recipe)
    return text.encode('utf-8'), recipe
