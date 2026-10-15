/* Private diagnostic entry, never selected for a production build. */
#include "sys.h"
#include "gfx.h"
#include "sound.h"
#include "system.h"
#include "video_backend.h"
#include "game_audio.h"
#include "sb_pcm.h"
#include <dpmi.h>
#include <pc.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    __dpmi_version_ret version;__dpmi_paddr before,after;
    __dpmi_get_version(&version);
    __dpmi_get_protected_mode_interrupt_vector(version.master_pic+7,&before);
    video_init();gfx_init();sys_init();sound_init();
    const char *mode=dos_audio_backend();unsigned int begin=g_ticks;
    int vga=1;
    for(int i=0;i<13;++i) {
        /* Four silent seconds, three isolated effects a second apart, then
         * quiet again. No stress triggers or rain in this playback check. */
        if(i>=4 && i<=6)
            sound_play(i==4 ? SFX_STONE : i==5 ? SFX_WOOD : SFX_CRUNCH,256,220,0);
        unsigned char *frame=g_fb;gfx_clear(frame,i*9);gfx_present();
        if(dos_video_verify(frame,FB_PITCH))vga=0;
        /* Intentionally slow foreground frames. The audio IRQ keeps running. */
        unsigned int until=g_ticks+100;
        while((int)(g_ticks-until)<0) {}
    }
    unsigned int elapsed=g_ticks-begin,blocks=dos_sb_completions,faults=dos_sb_stream_faults;
    dos_audio_shutdown();
    __dpmi_get_protected_mode_interrupt_vector(version.master_pic+7,&after);
    int restored=before.offset32==after.offset32 && before.selector==after.selector;
    int ok=restored && vga && !faults && (strstr(mode,"SB_") ? blocks>20 : !strcmp(mode,"PC_SPEAKER"));
    dos_system_shutdown();dos_video_restore();
    FILE *f=fopen("C:\\AUDIO.TXT","wb");if(!f)return 1;
    fprintf(f,"RESULT=%s\nBACKEND=%s\nBLOCKS=%u\nFAULTS=%u\nTICKS=%u\nVGA=%d\nIRQ_RESTORED=%d\n",
            ok?"PASS":"FAIL",mode,blocks,faults,elapsed,vga,restored);
    if(fclose(f))return 1;
    f=fopen("C:\\AUDIO.END","wb");if(!f)return 1;fprintf(f,"%s\n",ok?"PASS":"FAIL");fclose(f);
    return !ok;
}
