"""Mesh scan: give column-edge cells the fast neighbor path.

With one column per chunk, every cell with lx or lz equal to 0 or 15 (60 of
256 per layer) missed the unrolled interior path. It went through the generic
direction loop with four widx() calls per cell and never used the enclosed-
cell early-out. Edge cells between the bottom and top layers now use fixed
offsets inside the column; the cell across each seam is resolved by widx()
at most once per row and side. World edges and unloaded neighbors stay hidden
exactly as before, and the bottom/top layers keep the generic loop. Emitted
keys, masks and therefore meshes are unchanged.
"""
from build_imported import replace_once, lf_text

EDGE = '''				if(y>0 && y<WH-1)
				{
					/* Column seam or world edge: the generic loop's decisions,
					   with seam neighbors resolved once per row and side. */
					int same=(f&BF_SAMEHIDE),id=BLK_ID(b),nNX,nPX,nNZ,nPZ;
					if(0==x) nNX=-1;
					else if(lx>0) nNX=(int)i-WH;
					else { if(-2==rowNX) rowNX=widx(x-1,y,z); nNX=rowNX; }
					if(g_W-1==x) nPX=-1;
					else if(lx<CS-1) nPX=(int)i+WH;
					else { if(-2==rowPX) rowPX=widx(x+1,y,z); nPX=rowPX; }
					if(0==z) nNZ=-1;
					else if(lz>0) nNZ=(int)i-rowStride;
					else { if(-2==rowNZ) rowNZ=widx(x0,y,z-1); nNZ=rowNZ<0 ? -1 : rowNZ+lx*WH; }
					if(g_W-1==z) nPZ=-1;
					else if(lz<CS-1) nPZ=(int)i+rowStride;
					else { if(-2==rowPZ) rowPZ=widx(x0,y,z+1); nPZ=rowPZ<0 ? -1 : rowPZ+lx*WH; }
					if(hidesTab[g_blocks[i+1]] && hidesTab[g_blocks[i-1]] &&
					   (nNX<0 || hidesTab[g_blocks[nNX]]) && (nPX<0 || hidesTab[g_blocks[nPX]]) &&
					   (nNZ<0 || hidesTab[g_blocks[nNZ]]) && (nPZ<0 || hidesTab[g_blocks[nPZ]]))
					{
						continue;
					}
#define EFACE(D,N) \\
					if((N)>=0) \\
					{ \\
						u8 nb=g_blocks[N]; \\
						if(!hidesTab[nb] && !(same && BLK_ID(nb)==id)) \\
						{ \\
							key[D][lz*16+lx]=(u16)((def->tex[D]|(g_light[N]<<8))+1); \\
							if(D<=DIR_PX) mask[D][lx]|=1<<lz; else mask[D][lz]|=1<<lx; \\
							used|=1<<(D); \\
						} \\
					}
					EFACE(DIR_PY,(int)i+1)
					EFACE(DIR_NY,(int)i-1)
					EFACE(DIR_NX,nNX)
					EFACE(DIR_PX,nPX)
					EFACE(DIR_NZ,nNZ)
					EFACE(DIR_PZ,nPZ)
#undef EFACE
					continue;
				}
'''


def adapt(name, data):
    if name != 'world.c':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    inner = ('\t\t\tint inner=(y>0 && y<WH-1 && z>0 && z<g_W-1 && '
             '(!g_columnMap || (lz>0 && lz<CS-1)));\n')
    text = replace_once(text, inner,
        inner + '\t\t\t/* Lazily resolved seam rows (-2: not yet looked up). */\n'
                '\t\t\tint rowNX=-2,rowPX=-2,rowNZ=-2,rowPZ=-2;\n', recipe)
    loop = ('\t\t\t\tfor(d=0; d<6; ++d)\n'
            '\t\t\t\t{\n'
            '\t\t\t\t\tu8 nb;\n'
            '\t\t\t\t\tu32 ni;\n')
    text = replace_once(text, loop, EDGE + loop, recipe)
    return text.encode('utf-8'), recipe
