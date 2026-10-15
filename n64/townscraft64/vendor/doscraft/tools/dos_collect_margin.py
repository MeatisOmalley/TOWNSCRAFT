"""Conservative orientation margin with exact painter-axis invalidation."""
from build_imported import replace_once

def adapt(name,data,*,very_low_only=False):
    if name!='render.c': return data,[]
    text=data.decode('utf-8'); recipe=[]
    if '\r\n' in text:
        text=replace_once(text,'\r\n','\n',recipe)
    def one(before,after):
        nonlocal text
        text=replace_once(text,before,after,recipe)
    one('static int skyDarkenCur;','''static int skyDarkenCur;
static int collectReference,collectWide;
static int cBX=-1,cBY,cBZ,cYaw,cPitch,cScale,cView,cFloor,cWide;
static int cEdge,cX,cY,cZ;
static u32 cMesh;''')
    one('int fpx=focal16>>4,hwp=vw/2+vw/16,hhp=vh/2+vh/16;',
        'int fpx=focal16>>4,hwp=vw/2+vw/16,hhp=vh/2+vh/16;\n'
        '\t/* A changing mesh cannot benefit from orientation reuse. */\n'
        '\tcollectWide=!collectReference && g_meshVersion==cMesh &&\n'
        '\t    g_renderScale==cScale && g_viewDist==cView;')
    one('/* A steep downward view', '''/* Only collection uses the larger planes. Every cached draw retains the
   original tight early rejection, avoiding extra projection/clipping work.
   Doubled half-extents give >14 degrees of corner margin; total pose reuse
   is bounded to 11.25 degrees. The actual FOV/projection is unchanged. */
static void collect_planes(int wide)
{
	int f=focal16>>4,w=wide ? vw : vw/2+vw/16,h=wide ? vh : vh/2+vh/16;
	for(int axis=0;axis<3;++axis) {
		V3 *T=axis==0 ? TX : axis==1 ? TY : TZ;
		int **P=axis==0 ? PX : axis==1 ? PY : PZ;
		int begin=axis==0 ? tabX0 : axis==1 ? 0 : tabZ0;
		int end=axis==0 ? tabX1 : axis==1 ? WH : tabZ1;
		for(int i=begin;i<=end;++i) {
			int fx=f*T[i].c[0],fy=f*T[i].c[1],wz=w*T[i].c[2],hz=h*T[i].c[2];
			P[1][i]=fx-wz; P[2][i]=-fx-wz; P[3][i]=fy-hz; P[4][i]=-fy-hz;
		}
	}
	planeLen[1]=planeLen[2]=isqrt((u32)(f*f+w*w));
	planeLen[3]=planeLen[4]=isqrt((u32)(f*f+h*h));
}

/* A steep downward view''')
    one('\t\tstatic int cBX=-1,cBY,cBZ,cYaw,cPitch,cScale,cView,cFloor;\n\t\tstatic u32 cMesh;\n','')
    if 'camBX!=cBX || camBY!=cBY || camBZ!=cBZ' in text:
        one('camBX!=cBX || camBY!=cBY || camBZ!=cBZ',
            '(cEdge && (g_cam.x!=cX || g_cam.y!=cY || g_cam.z!=cZ)) || camBX!=cBX || camBY!=cBY || camBZ!=cBZ')
    else:
        one('(camBX!=cBX ? 1u<<CW_X : 0)',
            '((camBX!=cBX || (cEdge && (g_cam.x!=cX || g_cam.y!=cY || g_cam.z!=cZ))) ? 1u<<CW_X : 0)')
    # This also updates the benchmark-only invalidation census if present.
    angle='dyaw>8 || ABS(g_cam.pitch-cPitch)>8'
    test='(cWide ? dyaw+ABS(g_cam.pitch-cPitch)>32 : ('+angle+'))'
    if angle in text:
        one(angle,test+' || (ABS(rot[2][0])>ABS(rot[2][2]))!=nestX || collectReference')
    else:
        one('dyaw>8 ? 1u<<CW_YAW : 0',
            '(collectReference || (ABS(rot[2][0])>ABS(rot[2][2]))!=nestX || (cWide ? dyaw+ABS(g_cam.pitch-cPitch)>32 : dyaw>8)) ? 1u<<CW_YAW : 0')
        one('ABS(g_cam.pitch-cPitch)>8 ? 1u<<CW_PITCH : 0',
            '(!cWide && ABS(g_cam.pitch-cPitch)>8) ? 1u<<CW_PITCH : 0')
    fallback='''
			/* Extra off-screen faces must never consume the visible list's
			   capacity. Fall back to the original planes on saturation. */
			if(collectWide && nItems==maxItems) {
				collectReference=1; collectWide=0; collect_planes(0); nItems=0; collect();
				collectReference=0;
			}
			if(collectWide) collect_planes(0);
'''
    if '\t\t\tcollect();\n' in text:
        one('\t\t\tcollect();\n','\t\t\tif(collectWide) collect_planes(1);\n\t\t\tcollect();\n'+fallback)
    else:
        one('collect(); bench_cw_time(CW_COLLECT,','if(collectWide) collect_planes(1); collect();\n'+fallback+'\t\t\tbench_cw_time(CW_COLLECT,')
    one('\t\t\tcYaw=g_cam.yaw; cPitch=g_cam.pitch;',
        '\t\t\tcYaw=g_cam.yaw; cPitch=g_cam.pitch; cWide=collectWide;\n'
        '\t\t\t/* Strict back-face tests change just after an exact grid boundary. */\n'
        '\t\t\tcX=g_cam.x; cY=g_cam.y; cZ=g_cam.z;\n'
        '\t\t\tcEdge=!(cX&255) || !(cY&255) || !(cZ&255);')
    one('\tint t,i;\n\tTX=heap_alloc_low', '\tint t,i;\n\tcBX=-1;\n\tTX=heap_alloc_low')
    tail='''
#ifdef DOS_COLLECT_VERIFY
/* Independent tight-frustum redraw after the real cached draw, preserving the
 * candidate's retained list so the next frame exercises rotation reuse too. */
unsigned dos_collect_verify_frames,dos_collect_verify_failures;
unsigned dos_collect_verify_poses[32][8];
static Item *collectSaved;
static u32 *collectKeys;
static u16 *collectOrder;
static u8 *collectExpected;
void render_frame(u8 *fb,const RenderEnv *env)
{
    if(!collectExpected) {
        collectSaved=heap_alloc_high(8000*sizeof(Item));
        collectKeys=heap_alloc_high(8000*4); collectOrder=heap_alloc_high(8000*2);
        collectExpected=heap_alloc_high(FB_PITCH*VIEW_H);
    }
    int saved[]={cBX,cBY,cBZ,cYaw,cPitch,cScale,cView,cFloor,cWide,nestX,nStatic,cEdge,cX,cY,cZ};
    u32 mesh=cMesh;
    collectReference=0;
    collect_render_frame(fb,env);
    saved[0]=cBX; saved[1]=cBY; saved[2]=cBZ; saved[3]=cYaw; saved[4]=cPitch;
    saved[5]=cScale; saved[6]=cView; saved[7]=cFloor; saved[8]=cWide;
    saved[9]=nestX; saved[10]=nStatic; mesh=cMesh;
    saved[11]=cEdge; saved[12]=cX; saved[13]=cY; saved[14]=cZ;
    memcpy(collectExpected,fb,FB_PITCH*VIEW_H);
    memcpy(collectSaved,items,nStatic*sizeof(Item));
    memcpy(collectKeys,itemKey,nStatic*4); memcpy(collectOrder,order,nStatic*2);
    collectReference=1;
    collect_render_frame(fb,env);
    ++dos_collect_verify_frames;
    /* Doubled view rows may be intentionally unfinished. Compare the exact
     * produced rows; the presenter duplicates them independently. */
    int failed=0;
    for(int row=0;row<VIEW_H;row+=g_renderScale==2 ? 2 : 1)
        if(memcmp(collectExpected+row*FB_PITCH,fb+row*FB_PITCH,SCR_W)) failed=1;
    if(failed && dos_collect_verify_failures<32) {
        unsigned *p=dos_collect_verify_poses[dos_collect_verify_failures];
        p[0]=g_cam.x; p[1]=g_cam.y; p[2]=g_cam.z; p[3]=g_cam.yaw;
        p[4]=g_cam.pitch; p[5]=saved[3]; p[6]=saved[4]; p[7]=saved[10];
    }
    dos_collect_verify_failures+=failed;
    cBX=saved[0]; cBY=saved[1]; cBZ=saved[2]; cYaw=saved[3]; cPitch=saved[4];
    cScale=saved[5]; cView=saved[6]; cFloor=saved[7]; cWide=saved[8];
    nestX=saved[9]; nStatic=nItems=saved[10]; cMesh=mesh;
    cEdge=saved[11]; cX=saved[12]; cY=saved[13]; cZ=saved[14];
    memcpy(items,collectSaved,nStatic*sizeof(Item));
    memcpy(itemKey,collectKeys,nStatic*4); memcpy(order,collectOrder,nStatic*2);
    collectReference=0;
}
#endif
'''
    one('void render_frame(u8 *fb,const RenderEnv *env)\n',
        '#ifdef DOS_COLLECT_VERIFY\n#define render_frame collect_render_frame\n#endif\nvoid render_frame(u8 *fb,const RenderEnv *env)\n')
    text=replace_once(text,text,text+'\n#undef render_frame\n'+tail,recipe)
    if very_low_only:
        one('#include "render.h"\n', '#include "render.h"\n#include "graphics_presets.h"\n')
        one('static int skyDarkenCur;', 'static int skyDarkenCur;\nstatic int collectVeryLow,cVeryLow;')
        one('collectWide=!collectReference && g_meshVersion==cMesh &&',
            'collectVeryLow=dos_potato_mode;\n'
            '\tcollectWide=collectVeryLow && !collectReference && g_meshVersion==cMesh &&')
        one('(cEdge && (g_cam.x!=cX || g_cam.y!=cY || g_cam.z!=cZ))',
            '(collectVeryLow && cEdge && (g_cam.x!=cX || g_cam.y!=cY || g_cam.z!=cZ))')
        one('(ABS(rot[2][0])>ABS(rot[2][2]))!=nestX',
            '(collectVeryLow && ((ABS(rot[2][0])>ABS(rot[2][2]))!=nestX))')
        if 'unsigned cwReason=' in text:
            one('(camBX!=cBX || (collectVeryLow && cEdge',
                '(collectVeryLow!=cVeryLow || camBX!=cBX || (collectVeryLow && cEdge')
        else:
            one('if((collectVeryLow && cEdge',
                'if(collectVeryLow!=cVeryLow || (collectVeryLow && cEdge')
        one('cEdge=!(cX&255) || !(cY&255) || !(cZ&255);',
            'cEdge=collectVeryLow && (!(cX&255) || !(cY&255) || !(cZ&255));\n'
            '\t\t\tcVeryLow=collectVeryLow;')
    return text.encode(),recipe
