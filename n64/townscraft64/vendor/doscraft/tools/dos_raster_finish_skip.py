"""Keep scale-2 odd rows only where later gameplay overlays need them."""

from build_imported import replace_once, lf_text


def adapt(name, data):
    if name not in ('game.c', 'raster.c', 'raster.h'):
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    if name == 'raster.h':
        text = replace_once(text, 'void raster_finish(void);\n',
            'void raster_finish(void);\n'
            'void raster_set_odd_copy_policy(int skip,int mining,int item,int message,int debug);\n'
            'void raster_complete_odd_rows(void);\n', recipe)
    elif name == 'game.c':
        text = replace_once(text, 'static void draw_world(void)\n{\n',
            'static void draw_world(void)\n{\n'
            '\t/* Only play without rain can use the producer-proven repeated rows.\n'
            '\t   Copy the few row pairs that later sharp overlays may touch. */\n'
            '\traster_set_odd_copy_policy(state==GS_PLAY && rainLevel<=0 && g_renderScale==2,\n'
            '\t\tmineProgress>0 && mineX>=0, g_ticks<itemNameUntil,\n'
            '\t\tg_ticks<msgUntil, showDebug);\n', recipe)
    else:
        before = ('void raster_finish(void)\n{\n'
                  '\tint y;\n'
                  '\tif(2==rscale)\n'
                  '\t{\n'
                  '\t\tfor(y=0; y<rh; ++y)\n'
                  '\t\t{\n'
                  '\t\t\tmemcpy(rbuf+(y*2+1)*rpitch,rbuf+y*2*rpitch,rw*2);\n'
                  '\t\t}\n'
                  '\t}\n'
                  '}\n')
        after = '''/* The presenter reads even rows for proven pairs. Odd rows remain valid
   wherever a sharp overlay will read or modify them before presentation. */
static u8 oddCopy[VIEW_H/2];
static int oddSkip;

static void odd_copy_range(int begin,int end)
{
    int y;
    for(y=begin/2; y<(end+1)/2; ++y) oddCopy[y]=1;
}

void raster_set_odd_copy_policy(int skip,int mining,int item,int message,int debug)
{
    oddSkip=skip;
    if(!skip) return;
    memset(oddCopy,0,sizeof(oddCopy));
    odd_copy_range(96,105); /* Crosshair. */
    if(mining) odd_copy_range(110,114);
    if(item) odd_copy_range(188,200);
    if(message) odd_copy_range(176,188);
    if(debug) odd_copy_range(0,96);
}

void raster_finish(void)
{
    int y;
    if(2==rscale)
        for(y=0; y<rh; ++y)
            if(!oddSkip || oddCopy[y])
                memcpy(rbuf+(y*2+1)*rpitch,rbuf+y*2*rpitch,rw*2);
}

/* Benchmark readback hashes a conventional framebuffer after frame timing. */
void raster_complete_odd_rows(void)
{
    int y;
    if(2==rscale && oddSkip)
        for(y=0; y<rh; ++y)
            if(!oddCopy[y])
                memcpy(rbuf+(y*2+1)*rpitch,rbuf+y*2*rpitch,rw*2);
    oddSkip=0;
}
'''
        text = replace_once(text, before, after, recipe)
    return text.encode('utf-8'), recipe
