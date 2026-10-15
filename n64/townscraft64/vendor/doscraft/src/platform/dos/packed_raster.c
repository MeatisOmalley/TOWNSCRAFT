/* Independent DOS experiment: compact logical pixels, expand once at finish.
 * Mode X transport remains unchanged. This is not a shipping default.
 */
#include "packed_raster.h"
#include "raster.h"
#include <string.h>

static unsigned char logical[160*100];
static unsigned char *output;
static int active,width,height,pitch;

void dos_packed_begin(unsigned char *fb,int w,int h,int p,int scale)
{
    int x,y;
    active=0;
    if(scale!=2 || w<1 || w>160 || h<1 || h>100 || p<w*2 || !fb) return;
    output=fb; width=w; height=h; pitch=p;
    /* The original sky pass has established equal horizontal pairs. Preserve
     * its complete result (including stars/sun/moon), not just its flat color.
     */
    for(y=0;y<h;++y) for(x=0;x<w;++x)
        logical[y*w+x]=fb[y*2*p+x*2];
    raster_set_target(logical,w,h,w,1);
    active=1;
}

void __real_raster_finish(void);

void __wrap_raster_finish(void)
{
    int x,y;
    __real_raster_finish();
    if(!active) return;
    for(y=0;y<height;++y) {
        unsigned char *dst=output+y*2*pitch;
        const unsigned char *src=logical+y*width;
        for(x=0;x<width;++x) {
            dst[x*2]=src[x]; dst[x*2+1]=src[x];
        }
        memcpy(dst+pitch,dst,width*2);
    }
    raster_set_target(output,width,height,pitch,2);
    active=0;
}
