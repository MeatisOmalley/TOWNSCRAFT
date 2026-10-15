"""DOS placement fix for replaceable plants selected by the raycast."""

from build_imported import replace_once


def adapt(name, data):
    if name != 'game.c':
        return data, []
    recipe = []
    text = data.decode('utf-8')
    text = replace_once(text,
        '\tint item=held_item(),px=tX+tNX,py=tY+tNY,pz=tZ+tNZ,block;\n'
        '\tif(!item)\n',
        '\tint item=held_item(),px=tX+tNX,py=tY+tNY,pz=tZ+tNZ,block;\n'
        '\tint plant=targetValid && (B_FLOWER==BLK_ID(wget(tX,tY,tZ)) ||\n'
        '\t                           B_TALLGRASS==BLK_ID(wget(tX,tY,tZ)));\n'
        '\tif(plant) { px=tX; py=tY; pz=tZ; }\n'
        '\tif(!item)\n', recipe)
    text = replace_once(text,
        '\t\tif(tNY!=1 || !(blk_flags(wget(tX,tY,tZ))&BF_OPAQUE))\n',
        '\t\tif(plant ? !(blk_flags(wget(px,py-1,pz))&BF_OPAQUE) :\n'
        '\t\t   (tNY!=1 || !(blk_flags(wget(tX,tY,tZ))&BF_OPAQUE)))\n', recipe)
    text = replace_once(text,
        '\t\tu8 support=wget(tX,tY,tZ);\n'
        '\t\tint meta=0;\n'
        '\t\tif(!(blk_flags(support)&BF_OPAQUE) || -1==tNY)\n',
        '\t\tu8 support=plant ? wget(px,py-1,pz) : wget(tX,tY,tZ);\n'
        '\t\tint meta=0;\n'
        '\t\tif(!(blk_flags(support)&BF_OPAQUE) || (!plant && -1==tNY))\n', recipe)
    text = replace_once(text,
        '\t\tif(tNX==1) meta=1;\n'
        '\t\telse if(tNX==-1) meta=2;\n'
        '\t\telse if(tNZ==1) meta=3;\n'
        '\t\telse if(tNZ==-1) meta=4;\n',
        '\t\tif(!plant)\n'
        '\t\t{\n'
        '\t\t\tif(tNX==1) meta=1;\n'
        '\t\t\telse if(tNX==-1) meta=2;\n'
        '\t\t\telse if(tNZ==1) meta=3;\n'
        '\t\t\telse if(tNZ==-1) meta=4;\n'
        '\t\t}\n', recipe)
    return text.encode('utf-8'), recipe
