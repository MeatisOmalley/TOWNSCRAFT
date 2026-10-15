"""Opt-in exact reverse-painter experiment; never edits the vendor snapshot."""
from build_imported import replace_once


FRONTEND = r'''
/* Capture only the finalized primitives of ONE item. Reversing items AND
 * these commands is the exact inverse of the existing stable painter, not
 * a new depth heuristic. Overflow discards the attempt and redraws everything.
 */
static struct DosF2BCapture f2bCapture;
static int f2bCapturing,f2bFailed;
unsigned dos_f2b_fallbacks,dos_f2b_full_stops;
#ifdef DOS_F2B_VERIFY
unsigned dos_f2b_verify_failures,dos_f2b_verify_frames,dos_f2b_verify_fallbacks;
static u8 f2bExpected[FB_PITCH*VIEW_H];
#endif

static void f2b_emit(const struct DosF2BCommand *cmd,void *context)
{
	int i,minx,maxx,miny,maxy;
	(void)context;
	if(cmd->kind==DOS_F2B_POINT)
	{
		raster_pixel(cmd->vertices[0].x,cmd->vertices[0].y,(u8)cmd->color);
		return;
	}
	minx=maxx=cmd->vertices[0].x; miny=maxy=cmd->vertices[0].y;
	for(i=1;i<cmd->count;++i)
	{
		minx=MIN(minx,cmd->vertices[i].x); maxx=MAX(maxx,cmd->vertices[i].x);
		miny=MIN(miny,cmd->vertices[i].y); maxy=MAX(maxy,cmd->vertices[i].y);
	}
	/* Conservative enclosure only queries coverage; it never claims pixels. */
	if(dos_f2b_hidden_rect((minx-16)>>4,(miny-16)>>4,
	                      ((maxx+16)>>4)+1,((maxy+16)>>4)+1)) return;
	if(cmd->kind==DOS_F2B_TEXTURED)
		raster_poly(cmd->vertices,cmd->count,cmd->tile,cmd->flags);
	else raster_flat_poly(cmd->vertices,cmd->count,(u8)cmd->color);
}

static int draw_front_to_back(const RenderEnv *env)
{
	int key=0,ent=0;
	u32 facesBefore=g_statFaces,pixelsBefore=g_statPixels;
	dos_f2b_fallbacks=dos_f2b_full_stops=0;
	if(!dos_f2b_begin(vw,vh)) return 0;
	occQuery=0;
	while(key<nStatic || ent<nEnt)
	{
		Item *it;
		dos_f2b_capture_reset(&f2bCapture);
		f2bFailed=0; f2bCapturing=1;
		/* Terrain goes first on ties: reverse of entity-first painter. */
		if(ent<nEnt && (key>=nStatic || entKey[ent]<itemKey[order[key]]))
		{
			occCurrentKey=entKey[ent];
			BENCH_HOOK(++g_benchCnt[S_BOXES]);
			draw_mbox(&boxes[entBox[ent++]]);
		}
		else
		{
			int kind,x,y,z;
			occCurrentKey=itemKey[order[key]];
			it=&items[order[key++]];
			kind=(it->a>>25)&3;
			x=it->a&255; z=(it->a>>8)&255; y=(it->a>>16)&63;
			if(kind==IK_MODEL)
			{
				BENCH_HOOK(++g_benchCnt[S_MODELS]);
				draw_model(x,y,z,(u8)it->b);
			}
			else draw_quad(x,y,z,(it->a>>22)&7,((it->b>>16)&15)+1,
			               ((it->b>>20)&15)+1,it->b&255,(it->b>>8)&255);
		}
		f2bCapturing=0;
#ifdef DOS_F2B_STRESS
		/* Genuine bounded-buffer overflow once per DOS process: the prefix
		 * must never leak into the image; original full-frame fallback runs. */
		{ static int forced; if(!forced) {
			while(!f2bCapture.overflow) dos_f2b_capture_point(&f2bCapture,0,0,0);
			forced=1;
		} }
#endif
		if(f2bFailed || dos_f2b_capture_replay(&f2bCapture,1,f2b_emit,NULL)!=1)
		{
			dos_f2b_end();
			++dos_f2b_fallbacks;
			g_statFaces=facesBefore; g_statPixels=pixelsBefore;
			draw_sky(env); draw_clouds(env);
			return 0;
		}
		if(dos_f2b_full()) { ++dos_f2b_full_stops; break; }
	}
	/* The outline and HUD remain painter overlays, outside coverage. */
	dos_f2b_end();
	return 1;
}

#ifdef DOS_F2B_STRESS
static void f2b_stress_boxes(void)
{
	int i,j;
	/* Three overlapping boxes at/either side of the eye's cell exercise
	 * near clipping, cutouts, rotation, stable entity ties and opaque zero.
	 * Diagnostic build ONLY. Original painter sees the identical boxes. */
	for(i=0;i<3;++i)
	{
		MBox *b=render_add_box();
		if(!b) return;
		memset(b,0,sizeof(*b));
		b->ox=g_cam.x+(fsin(g_cam.yaw)*(96+i*96)>>14);
		b->oz=g_cam.z+(fcos(g_cam.yaw)*(96+i*96)>>14);
		b->oy=g_cam.y-128;
		b->x0=-8; b->x1=8; b->y0=0; b->y1=16; b->z0=-8; b->z1=8;
		b->yaw=g_cam.yaw+i*79; b->pitch=i*23;
		b->hw=256; b->h=256; b->light=15;
		for(j=0;j<6;++j) b->tex[j]=i==1 ? T_GLASS : i==2 ? T_LEAVES : T_STONE;
	}
}
#endif

'''


def adapt(name, data):
    if name not in ('render.c', 'raster.c'):
        return data, []
    recipe=[]
    text=data.decode('utf-8')
    if '\r\n' in text:
        recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
        text=text.replace('\r\n','\n')
    def change(before, after):
        nonlocal text
        text=replace_once(text,before,after,recipe)
    change('#include "raster.h"\n', '#include "raster.h"\n#include "f2b_raster.h"\n' +
           ('#include "f2b_capture.h"\n' if name=='render.c' else ''))
    if name=='render.c':
        change('static int camBX,camBY,camBZ;\n',
               'static int camBX,camBY,camBZ;\nstatic int f2bCapturing,f2bFailed;\n'
               'static struct DosF2BCapture f2bCapture;\n')
        change('\t\t\traster_pixel((minx+maxx)>>5,(miny+maxy)>>5,flat);\n',
               '\t\t\tif(f2bCapturing) { if(dos_f2b_capture_point(&f2bCapture,(minx+maxx)>>5,(miny+maxy)>>5,flat)!=1) f2bFailed=1; }\n'
               '\t\t\telse raster_pixel((minx+maxx)>>5,(miny+maxy)>>5,flat);\n')
        change('\t\traster_poly(sv,m,tex,flags);\n',
               '\t\tif(f2bCapturing) { if(dos_f2b_capture_poly(&f2bCapture,sv,m,tex,flags)!=1) f2bFailed=1; }\n'
               '\t\telse raster_poly(sv,m,tex,flags);\n')
        change('\t\traster_flat_poly(sv,m,flat);\n',
               '\t\tif(f2bCapturing) { if(dos_f2b_capture_flat(&f2bCapture,sv,m,flat)!=1) f2bFailed=1; }\n'
               '\t\telse raster_flat_poly(sv,m,flat);\n')
        # Declarations must precede draw_spoly; functions follow draw_mbox.
        functions=FRONTEND.replace('static struct DosF2BCapture f2bCapture;\nstatic int f2bCapturing,f2bFailed;\n','')
        change('void render_frame(u8 *fb,const RenderEnv *env)\n',
               functions+'void render_frame(u8 *fb,const RenderEnv *env)\n')
        change('\tint key,ent;\n\toccQuery=0; g_statOccluded=0;\n',
               '\tint key,ent;\n#ifdef DOS_F2B_STRESS\n\tf2b_stress_boxes();\n#endif\n'
               '\toccQuery=0; g_statOccluded=0;\n')
        change('\t/* Far to near: merge the (cached) face list with the entity list.  On\n',
               '\tint frontSuccess=draw_front_to_back(env);\n'
               '#ifdef DOS_F2B_VERIFY\n'
               '\tif(frontSuccess) { memcpy(f2bExpected,fb,sizeof(f2bExpected)); draw_sky(env); draw_clouds(env); }\n'
               '\telse ++dos_f2b_verify_fallbacks;\n'
               '#endif\n'
               '\tif(!frontSuccess\n#ifdef DOS_F2B_VERIFY\n || 1\n#endif\n)\n\t{\n'
               '\t/* Far to near: merge the (cached) face list with the entity list.  On\n')
        change('\tg_prof[2]+=g_ticks-t;}\n',
               '\t}\n'
               '#ifdef DOS_F2B_VERIFY\n'
               '\tif(frontSuccess) { int y,bad=0; ++dos_f2b_verify_frames;\n'
               '\t\tfor(y=0;y<vh;++y) if(memcmp(f2bExpected+y*g_renderScale*FB_PITCH,fb+y*g_renderScale*FB_PITCH,SCR_W)) bad=1;\n'
               '\t\tif(bad) ++dos_f2b_verify_failures; }\n'
               '#endif\n'
               '\tg_prof[2]+=g_ticks-t;}\n')
    else:
        change('\trscale=scale;\n', '\trscale=scale;\n\tdos_f2b_set_target(buf,w,h,pitch,scale);\n')
        change('\t\t\t\t\t\tif(xr>xl) flat_span(row+xl*rscale,(xr-xl)*rscale,flatWord);\n',
               '\t\t\t\t\t\tif(xr>xl) {\n'
               '\t\t\t\t\t\t\tif(dos_f2b_active) dos_f2b_flat_row(row,xl,xr,s,rscale,flat);\n'
               '\t\t\t\t\t\t\telse flat_span(row+xl*rscale,(xr-xl)*rscale,flatWord);\n'
               '\t\t\t\t\t\t}\n')
        change('\t\t\t\t\t\tmemset(row+xl*rscale,flat,(xr-xl)*rscale);\n',
               '\t\t\t\t\t\tif(dos_f2b_active) dos_f2b_flat_row(row,xl,xr,s,rscale,flat);\n'
               '\t\t\t\t\t\telse memset(row+xl*rscale,flat,(xr-xl)*rscale);\n')
        change('void raster_pixel(int x,int y,u8 color)\n{\n',
               'void raster_pixel(int x,int y,u8 color)\n{\n'
               '\tif(dos_f2b_active && dos_f2b_claim_point(x,y)!=1) return;\n')
    return text.encode('utf-8'),recipe
