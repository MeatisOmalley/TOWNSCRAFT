"""Explicit DOS-only source overlays; the pinned Towns snapshot stays immutable.

Every change is a replayable, fail-closed replacement in the build manifest.
Experiments remain independently selectable; the direct-port builder defaults
to no overlays. No automatic copying from a different local Towns branch.
"""

from build_imported import replace_once


def adapt(name, data, *, graphics=False, raster_zero_dx=False, camera_reuse=False,
          packed_raster=False, early_split_cull=False, front_to_back=False, edge_recip=False,
          trap_zero_gradient=False, trap_destination=False,split_reuse=False,depth_adaptive=False,trap_stable=False,trap_four=False,wall_visibility=False,wall_verify=False,group_facing=False,proven_rows=False,hud_proof=False,raster_finish_skip=False,very_low_texels=False):
    recipe=[]
    if raster_finish_skip:
        import dos_raster_finish_skip
        data,steps=dos_raster_finish_skip.adapt(name,data)
        recipe.extend(steps)
    if proven_rows:
        import dos_proven_rows
        data,steps=dos_proven_rows.adapt(name,data)
        recipe.extend(steps)
    if group_facing:
        import dos_group_facing
        data,steps=dos_group_facing.adapt(name,data)
        recipe.extend(steps)
    if wall_verify and not wall_visibility:
        raise ValueError('Wall verification requires its candidate')
    if wall_visibility:
        if front_to_back:
            raise ValueError('Measure wall rejection separately from the losing coverage renderer')
        import dos_wall_visibility
        data,steps=dos_wall_visibility.adapt(name,data,verify=wall_verify)
        recipe.extend(steps)
    if sum((trap_stable,trap_four,trap_zero_gradient,trap_destination))>1:
        raise ValueError('Measure trap probes independently before combining')
    if trap_four:
        import dos_trap_four
        data,steps=dos_trap_four.adapt(name,data)
        recipe.extend(steps)
    if very_low_texels:
        import dos_very_low_texels
        data,steps=dos_very_low_texels.adapt(name,data)
        recipe.extend(steps)
    if trap_stable:
        if trap_zero_gradient or trap_destination:
            raise ValueError('Measure row setup probes independently before combining')
        import dos_trap_stable
        data,steps=dos_trap_stable.adapt(name,data)
        recipe.extend(steps)
    if name=='trap.S' and trap_destination:
        text=data.decode('utf-8')
        if '\r\n' in text:
            recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
            text=text.replace('\r\n','\n')
        text=replace_once(text,'\tmovl\t%eax,rowX\n',
            '\t/* EDI is unused by gradient/prestep setup. Keep the exact destination. */\n'
            '\tmovl\tT_ROW(%ebp),%edi\n'
            '\t.if \\SCALE==2\n\tleal\t(%edi,%eax,2),%edi\n'
            '\t.else\n\taddl\t%eax,%edi\n\t.endif\n',recipe)
        text=replace_once(text,
            '\tmovl\trowX,%eax\n\tshll\t$16,%eax\n\taddl\t$0x8000,%eax\n',
            '\t/* Left-clipped xl was clamped to zero above. */\n\tmovl\t$0x8000,%eax\n',recipe)
        text=replace_once(text,
            '\t/* Destination */\n\tmovl\tT_ROW(%ebp),%edi\n\tmovl\trowX,%eax\n'
            '\t.if \\SCALE==2\n\taddl\t%eax,%edi\n\t.endif\n\taddl\t%eax,%edi\n',
            '\t/* Destination already held in EDI; texture setup did not touch it. */\n',recipe)
        data=text.encode('utf-8')
    if name=='trap.S' and trap_zero_gradient:
        text=data.decode('utf-8')
        if '\r\n' in text:
            recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
            text=text.replace('\r\n','\n')
        for field,label in (('DU',31),('DV',32)):
            before='\tsarl\t$8,%eax\n\timull\t%ecx,%eax\n\tsarl\t$6,%eax\n\tmovl\t%eax,row'+field+'\n'
            after=('\tsarl\t$8,%eax\n'
                '\t/* Exact zero shifted delta: no multiply or rounding needed. */\n'
                '\ttestl\t%eax,%eax\n\tjz\t'+str(label)+'f\n'
                '\timull\t%ecx,%eax\n\tsarl\t$6,%eax\n'+str(label)+':\n'
                '\tmovl\t%eax,row'+field+'\n')
            text=replace_once(text,before,after,recipe)
        data=text.encode('utf-8')
    if graphics:
        import dos_graphics
        data, steps=dos_graphics.adapt(name,data)
        recipe.extend(steps)
    if name=='raster.c' and raster_zero_dx:
        text=data.decode('utf-8')
        if '\r\n' in text:
            recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
            text=text.replace('\r\n','\n')
        text=replace_once(text,
            '\te->dx=divshift(b->x-a->x,dy,16);\n',
            '\t/* Accepted polygon edges have dy>0. Exact vertical edges need no IDIV. */\n'
            '\te->dx=(b->x==a->x) ? 0 : divshift(b->x-a->x,dy,16);\n',recipe)
        data=text.encode('utf-8')
    if name=='render.c' and camera_reuse:
        text=data.decode('utf-8')
        if '\r\n' in text:
            recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
            text=text.replace('\r\n','\n')
        text=replace_once(text,'static int camBX,camBY,camBZ;\n',
            'static int camBX,camBY,camBZ;\n'
            'static int cameraReuseValid,cameraReuseView,cameraReuseWidth,cameraReuseScale;\n'
            'static Camera cameraReusePose;\n',recipe)
        text=replace_once(text,'void render_init(void)\n{\n\tint t,i;\n',
            'void render_init(void)\n{\n\tint t,i;\n\tcameraReuseValid=0;\n',recipe)
        text=replace_once(text,'static void setup_camera(void)\n{\n',
            'static void setup_camera(void)\n{\n'
            '\t/* Tables depend only on exact pose, world bounds and projection. */\n'
            '\tif(cameraReuseValid && cameraReusePose.x==g_cam.x && cameraReusePose.y==g_cam.y &&\n'
            '\t   cameraReusePose.z==g_cam.z && cameraReusePose.yaw==g_cam.yaw &&\n'
            '\t   cameraReusePose.pitch==g_cam.pitch && cameraReuseView==g_viewDist &&\n'
            '\t   cameraReuseWidth==g_W && cameraReuseScale==g_renderScale) return;\n'
            '\tcameraReusePose=g_cam; cameraReuseView=g_viewDist; cameraReuseWidth=g_W;\n'
            '\tcameraReuseScale=g_renderScale; cameraReuseValid=1;\n',recipe)
        data=text.encode('utf-8')
    if name=='render.c' and packed_raster:
        text=data.decode('utf-8')
        if '\r\n' in text:
            recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
            text=text.replace('\r\n','\n')
        text=replace_once(text,'#include "raster.h"\n',
            '#include "raster.h"\n#include "packed_raster.h"\n',recipe)
        text=replace_once(text,'\tdraw_sky(env);\n\tdraw_clouds(env);\n',
            '\tdraw_sky(env);\n'
            '#ifndef BENCH_OVERDRAW\n'
            '\tdos_packed_begin(fb,vw,vh,FB_PITCH,g_renderScale);\n'
            '#endif\n\tdraw_clouds(env);\n',recipe)
        data=text.encode('utf-8')
    if name=='render.c' and early_split_cull:
        text=data.decode('utf-8')
        if '\r\n' in text:
            recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
            text=text.replace('\r\n','\n')
        text=replace_once(text,'/* Is the box between grid lines xa..xb, ya..yb, za..zb wholly behind the\n',
            '/* Exact descendant gate: fixed-point weighted planes can have interior\n'
            '   minima lower than their endpoint minima. Confirm every included grid\n'
            '   line only when the cheap endpoint test suggests rejection. */\n'
            'static int split_parent_visible(int x,int y,int z,int dir,int ex,int ez)\n'
            '{\n'
            '\tint x1=x+ex,y1=y+1,z1=z+ez,k,i;\n'
            '\tif(dir==DIR_NX) x1=x;\n'
            '\telse if(dir==DIR_PX) x=x1;\n'
            '\telse if(dir==DIR_NY) y1=y;\n'
            '\telse if(dir==DIR_PY) y=y1;\n'
            '\telse if(dir==DIR_NZ) z1=z;\n'
            '\telse if(dir==DIR_PZ) z=z1;\n'
            '\tfor(k=0;k<NPLANES;++k)\n'
            '\t{\n'
            '\t\tint limit=k==0 ? -NEAR_Z : 0;\n'
            '\t\tint ax=MIN(PX[k][x],PX[k][x1]);\n'
            '\t\tint ay=MIN(PY[k][y],PY[k][y1]);\n'
            '\t\tint az=MIN(PZ[k][z],PZ[k][z1]);\n'
            '\t\tif(ax+ay+az<=limit) continue;\n'
            '\t\tfor(i=x+1;i<x1;++i) ax=MIN(ax,PX[k][i]);\n'
            '\t\tfor(i=z+1;i<z1;++i) az=MIN(az,PZ[k][i]);\n'
            '\t\tif(ax+ay+az>limit) return 0;\n'
            '\t}\n'
            '\treturn 1;\n'
            '}\n\n'
            '/* Is the box between grid lines xa..xb, ya..yb, za..zb wholly behind the\n',recipe)
        text=replace_once(text,
            '\t\tif(g_textureSubdivision==SUBDIV_FULL)\n',
            '\t\t/* Reject the whole parent before generating invisible children.\n'
            '\t\t   Confirm interior grid lines before trusting endpoint minima. */\n'
            '\t\tif(!split_parent_visible(x,y,z,dir,ex,ez))\n'
            '\t\t{\n\t\t\tBENCH_HOOK(++g_benchCnt[S_EARLY]);\n\t\t\treturn;\n\t\t}\n'
            '\t\tif(g_textureSubdivision==SUBDIV_FULL)\n',recipe)
        data=text.encode('utf-8')
    if name=='raster.c' and edge_recip:
        text=data.decode('utf-8')
        if '\r\n' in text:
            recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
            text=text.replace('\r\n','\n')
        text=replace_once(text,'#include "fmath.h"\n','#include "fmath.h"\n#include "edge_recip.h"\n',recipe)
        text=replace_once(text,'\trscale=scale;\n','\trscale=scale;\n\tdos_edge_recip_init();\n',recipe)
        before='divshift(b->x-a->x,dy,16)'
        text=replace_once(text,before,'dos_edge_recip_divshift(b->x-a->x,dy,16)',recipe)
        text=replace_once(text,'divshift(du,dy,4)','dos_edge_recip_divshift(du,dy,4)',recipe)
        text=replace_once(text,'divshift(dv,dy,4)','dos_edge_recip_divshift(dv,dy,4)',recipe)
        data=text.encode('utf-8')
    if split_reuse and depth_adaptive:
        raise ValueError('Measure subdivision probes independently before combining')
    if depth_adaptive:
        import dos_depth_adaptive
        data,steps=dos_depth_adaptive.adapt(name,data)
        recipe.extend(steps)
    if split_reuse:
        import dos_split_reuse
        data,steps=dos_split_reuse.adapt(name,data)
        recipe.extend(steps)
    if front_to_back:
        if packed_raster:
            raise ValueError('Front-to-back and packed-raster target layouts cannot be combined')
        import dos_front_to_back
        data,steps=dos_front_to_back.adapt(name,data)
        recipe.extend(steps)
    if name=='game.c':
        import dos_plant_placement
        data,steps=dos_plant_placement.adapt(name,data)
        recipe.extend(steps)
        import dos_crafting
        data,steps=dos_crafting.adapt(name,data)
        recipe.extend(steps)
    if hud_proof and name in ('game.c','ui.c'):
        import dos_hud_proof
        data,steps=dos_hud_proof.adapt(name,data)
        recipe.extend(steps)
    if name=='world.c':
        import dos_immediate_edit_light
        data,steps=dos_immediate_edit_light.adapt(name,data)
        recipe.extend(steps)
    return data,recipe
