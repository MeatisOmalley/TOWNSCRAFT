"""Explicit DOS-only source overlays; the pinned Towns snapshot stays immutable.

Every change is a replayable, fail-closed replacement in the build manifest.
Experiments remain independently selectable; the direct-port builder defaults
to no overlays. No automatic copying from a different local Towns branch.
"""

from build_imported import replace_once


def adapt(name, data, *, graphics=False, raster_zero_dx=False, camera_reuse=False,
          packed_raster=False):
    recipe=[]
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
    return data,recipe
