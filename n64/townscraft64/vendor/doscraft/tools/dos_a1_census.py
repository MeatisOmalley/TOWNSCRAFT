"""Diagnostic export of visible terrain items in painter order for A1 analysis."""

from build_imported import replace_once


def adapt(name, data):
    if name != 'render.c':
        return data, []
    recipe = []
    text = data.decode('utf-8')
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    text = replace_once(text, 'static int nBoxes;\n',
        'static int nBoxes;\n'
        '/* Benchmark-only per-item output; index is the collected item index. */\n'
        'static u16 a1FaceCount[8000];\n', recipe)
    text = replace_once(text, '\tg_statItems=nStatic+nEnt;\n',
        '\tg_statItems=nStatic+nEnt;\n'
        '\tmemset(a1FaceCount,0,nStatic*sizeof(a1FaceCount[0]));\n', recipe)
    text = replace_once(text,
        '\t\t/* Model metadata can move doors and other parts without publishing\n',
        '\t\tif(kind==IK_QUAD)\n'
        '\t\t\ta1FaceCount[it-items]=(u16)MIN(g_statFaces-facesBefore,65535u);\n'
        '\t\t/* Model metadata can move doors and other parts without publishing\n', recipe)
    text = replace_once(text, 'void render_frame(u8 *fb,const RenderEnv *env)\n',
        '''/* Dump sampled replay frames outside measured timing. Each record is
   a,b,painter-key,drawn-face-count in far-to-near draw order. */
#include <stdio.h>
int render_a1_dump(int scene,int sample)
{
    char path[32];
    FILE *file;
    u32 count=(u32)nStatic;
    int i,ok=1;
    if(scene<0 || scene>9 || sample<0 || sample>9 || nStatic<0 || nStatic>8000) return -1;
    sprintf(path,"C:\\\\A1S%dP%d.BIN",scene,sample);
    file=fopen(path,"wb");
    if(!file) return -1;
    if(fwrite(&count,sizeof(count),1,file)!=1) ok=0;
    for(i=nStatic-1; i>=0 && ok; --i)
    {
        int index=order[i];
        u32 record[4]={items[index].a,items[index].b,itemKey[index],a1FaceCount[index]};
        if(fwrite(record,sizeof(record),1,file)!=1) ok=0;
    }
    if(fclose(file)) ok=0;
    return ok ? 0 : -1;
}

void render_frame(u8 *fb,const RenderEnv *env)
''', recipe)
    return text.encode('utf-8'), recipe
