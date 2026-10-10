/* Real retained assets -> foreground mixer -> real SB1 transport. All written
 * bytes are independently reconstructed on the host. Not seamless/game audio. */
#include "sound.h"
#include "sound_assets.h"
#include "sound_mixer.h"
#include "sb_pcm.h"
#include "sys.h"
#include "system.h"
#include "../../../tests/sfx_pcm_fixture.inc"
#include <stdio.h>
static unsigned char pcm[SFX_FIXTURE_FRAMES];
static int wait_complete(void)
{
    unsigned int start=g_ticks,guard=20000000;
    while(dos_sb_busy() && g_ticks-start<200 && --guard) {}
    return guard && !dos_sb_busy();
}
int main(void)
{
    puts("DOScraft mixed Towns effects / SB1 diagnostic (not the game)");
    sys_init();
    unsigned int start=g_ticks,blocks=0,interrupts=0,dsp=0;
    int silent=1;
    int ok=dos_sound_assets_build()==0 && dos_mixer_init(DOS_SB_RATE)==0 && dos_sb_init()==0;
    if(ok) dsp=dos_sb_version();
    FILE *output=NULL;
    if(ok) { output=fopen("C:\\SFXPCM.BIN","wb"); ok=output!=NULL; }
    for(unsigned int block=0;block<SFX_FIXTURE_BLOCKS && ok;++block) {
        sfx_fixture_command(block);
        unsigned int count=dos_sb_completions;
        ok=dos_mixer_render(pcm,sizeof(pcm))==0 && dos_sb_submit(pcm,sizeof(pcm))==0;
        if(!ok) break;
        /* Exercise foreground DOS writes while playback/IRQ7 are active. */
        ok=fwrite(pcm,1,sizeof(pcm),output)==sizeof(pcm) && wait_complete() && dos_sb_completions==count+1;
        if(block>=40) for(unsigned int i=0;i<sizeof(pcm);++i) silent &= pcm[i]==128;
        if(ok) ++blocks;
    }
    if(output && fclose(output)) ok=0;
    interrupts=dos_sb_completions;
    dos_mixer_stop(); dos_sb_shutdown();
    unsigned int elapsed=g_ticks-start;
    dos_system_shutdown();
    ok &= silent && blocks==SFX_FIXTURE_BLOCKS && interrupts==blocks;
    FILE *report=fopen("C:\\SFXPCM.TXT","wb");
    if(!report) return 1;
    int written=fprintf(report,"DOScraft retained effects mixer/SB1 diagnostic\nDSP_VERSION=%u.%02u\n"
        "OUTPUT_RATE=%u\nBLOCKS=%u\nFRAMES_PER_BLOCK=%u\nREAL_COMPLETIONS=%u\n"
        "FINAL_SILENCE=%s\nELAPSED_100HZ_TICKS=%u\nAUDIBLE_QUALITY=UNTESTED\n"
        "CONTINUOUS_PLAYBACK=UNIMPLEMENTED\nOPL_MUSIC=UNIMPLEMENTED\nRESULT=%s\n",
        dsp>>8,dsp&255,DOS_SB_RATE,blocks,SFX_FIXTURE_FRAMES,interrupts,
        silent?"PASS":"FAIL",elapsed,ok?"PASS":"FAIL")>=0;
    if(fclose(report)) written=0;
    puts(ok && written ? "DOS mixed effects DMA: PASS" : "DOS mixed effects DMA: FAIL");
    return !(ok && written);
}
