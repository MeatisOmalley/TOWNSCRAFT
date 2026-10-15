"""Optional parent-local shared-edge projection reuse; immutable vendor source."""
from build_imported import replace_once,lf_text

CACHE='''/* Only the immediately previous, fully near-visible child of this parent.
   Store positions before draw_spoly can mutate them during guard clipping.
   UVs are deliberately not shared: each child retains its original mapping. */
static int splitReuseActive,splitReuseValid,splitReuseX,splitReuseZ,splitReuseEx,splitReuseEz;
static int splitReuseCamera[4][3],splitReuseScreen[4][2];
'''


def adapt(name,data):
    if name!='render.c': return data,[]
    recipe=[]
    text=lf_text(data,recipe)
    text=replace_once(text,'} CVert;\n','} CVert;\n\n'+CACHE,recipe)
    text=replace_once(text,'void render_init(void)\n{\n\tint t,i;\n',
        'void render_init(void)\n{\n\tint t,i;\n\tsplitReuseActive=splitReuseValid=0;\n',recipe)
    text=replace_once(text,'\t\tif(g_textureSubdivision==SUBDIV_FULL)\n',
        '\t\tsplitReuseValid=0;\n\t\tsplitReuseActive=!occCapture && !occQuery;\n'
        '\t\tif(g_textureSubdivision==SUBDIV_FULL)\n',recipe)
    text=replace_once(text,'\t\telse draw_quad_split(x,y,z,dir,ex,ez,tex,light);\n\t\treturn;\n',
        '\t\telse draw_quad_split(x,y,z,dir,ex,ez,tex,light);\n'
        '\t\tsplitReuseActive=splitReuseValid=0;\n\t\treturn;\n',recipe)
    text=replace_once(text,'\t\t\tBENCH_HOOK(++g_benchCnt[S_EARLY]);\n\t\t\treturn;\n\t\t}\n\t}\n\tint L=face_light_level(light,dir,skyDarkenCur);\n',
        '\t\t\tBENCH_HOOK(++g_benchCnt[S_EARLY]);\n\t\t\tsplitReuseValid=0;\n\t\t\treturn;\n\t\t}\n\t}\n\tint L=face_light_level(light,dir,skyDarkenCur);\n',recipe)
    text=replace_once(text,'\tCVert cv[4];\n\tRVert sv[12];\n\tint k,all=1;\n',
        '''\tCVert cv[4];
\tRVert sv[12];
\tint k,all=1,reuseAxis=-1,reuseMask=0;
\tif(splitReuseActive && splitReuseValid)
\t{
\t\tif(z==splitReuseZ && ez==splitReuseEz && x==splitReuseX+splitReuseEx) reuseAxis=0;
\t\telse if(x==splitReuseX && ex==splitReuseEx && z==splitReuseZ+splitReuseEz) reuseAxis=2;
\t}
''',recipe)
    before='''\t\tcv[k].x=px->c[0]+py->c[0]+pz->c[0];
\t\tcv[k].y=px->c[1]+py->c[1]+pz->c[1];
\t\tcv[k].z=px->c[2]+py->c[2]+pz->c[2];
'''
    after='''\t\tif(reuseAxis>=0 && !sel[reuseAxis])
\t\t{
\t\t\tint previous=k^((reuseAxis==2 && dir>DIR_PX) ? 3 : 1);
\t\t\tcv[k].x=splitReuseCamera[previous][0];
\t\t\tcv[k].y=splitReuseCamera[previous][1];
\t\t\tcv[k].z=splitReuseCamera[previous][2];
\t\t\tsv[k].x=splitReuseScreen[previous][0];
\t\t\tsv[k].y=splitReuseScreen[previous][1];
\t\t\treuseMask|=1<<k;
\t\t}
\t\telse
\t\t{
'''+''.join('\t'+line+'\n' for line in before.splitlines())+'''\t\t}
'''
    text=replace_once(text,before,after,recipe)
    text=replace_once(text,'\t\t\tproject(cv[k].x,cv[k].y,cv[k].z,&sv[k].x,&sv[k].y);\n',
        '\t\t\tif(!(reuseMask&(1<<k))) project(cv[k].x,cv[k].y,cv[k].z,&sv[k].x,&sv[k].y);\n',recipe)
    text=replace_once(text,'\t\tdraw_spoly(sv,4,tile,flags,g_shadeLUT[L][texAvg[tex]],nearDepth,farDepth);\n',
        '''\t\tif(splitReuseActive)
\t\t{
\t\t\tfor(k=0;k<4;++k)
\t\t\t{
\t\t\t\tsplitReuseCamera[k][0]=cv[k].x; splitReuseCamera[k][1]=cv[k].y; splitReuseCamera[k][2]=cv[k].z;
\t\t\t\tsplitReuseScreen[k][0]=sv[k].x; splitReuseScreen[k][1]=sv[k].y;
\t\t\t}
\t\t\tsplitReuseX=x; splitReuseZ=z; splitReuseEx=ex; splitReuseEz=ez; splitReuseValid=1;
\t\t}
\t\tdraw_spoly(sv,4,tile,flags,g_shadeLUT[L][texAvg[tex]],nearDepth,farDepth);
''',recipe)
    text=replace_once(text,'\t\tdraw_cpoly(cv,4,tile,flags,g_shadeLUT[L][texAvg[tex]]);\n',
        '\t\tsplitReuseValid=0;\n\t\tdraw_cpoly(cv,4,tile,flags,g_shadeLUT[L][texAvg[tex]]);\n',recipe)
    return text.encode('utf-8'),recipe
