"""Cheaper whole-world and bottom-chunk scans with unchanged visit order.

light_init() and world_rebuild_after_load() visit every x,z of the 1024
world and test each against the resident-column map; a save load runs
these loops five times over 1M positions for only 25 resident columns.
A non-resident column now skips its remaining 15 x positions at once; the
z-then-x order of every resident cell, and so the light queue, is unchanged.

The bottom-chunk seal/water test called widx() for each of 256 top cells and
each side cell layer. One column row starts at widx(x0,0,z), so cells are
addressed by offsets from that base, as in scan_chunk_mask().
"""
from build_imported import replace_once, lf_text


def adapt(name, data):
    if name != 'world.c':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    skip = '\t\t\tif(!world_column_ready(x/CS,z/CS)) continue;\n'
    count = text.count(skip)
    if count != 3:
        raise ValueError(f'Expected three whole-world column scans; found {count}')
    recipe.append(dict(operation='replace', before=skip, count=count,
        after='\t\t\t/* The rest of this column row is not resident either. */\n'
              '\t\t\tif(!world_column_ready(x/CS,z/CS)) { x|=CS-1; continue; }\n'))
    text = text.replace(skip, recipe[-1]['after'])
    text = replace_once(text,
        '\t\tfor(lz=0; lz<CS; ++lz)\n'
        '\t\t{\n'
        '\t\t\tfor(lx=0; lx<CS; ++lx)\n'
        '\t\t\t{\n'
        '\t\t\t\tint x=x0+lx,z=z0+lz;\n'
        '\t\t\t\tu32 i=widx(x,CS-1,z);\n',
        '\t\tfor(lz=0; lz<CS; ++lz)\n'
        '\t\t{\n'
        '\t\t\t/* All cells of this row are in one column: widx(x0+lx,y,z)\n'
        '\t\t\t   is rowBase+lx*WH+y. */\n'
        '\t\t\tu32 rowBase=widx(x0,0,z0+lz);\n'
        '\t\t\tfor(lx=0; lx<CS; ++lx)\n'
        '\t\t\t{\n'
        '\t\t\t\tu32 i=rowBase+lx*WH+CS-1;\n', recipe)
    text = replace_once(text,
        '\t\t\t\t\t\tif(B_WATER==BLK_ID(g_blocks[widx(x,ly,z)]))\n',
        '\t\t\t\t\t\tif(B_WATER==BLK_ID(g_blocks[rowBase+lx*WH+ly]))\n', recipe)
    return text.encode('utf-8'), recipe
