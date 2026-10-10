/* Single-cycle delivery/lifecycle diagnostic, not game or seamless audio.
 * Test ramp is deliberately not a Towns effect. Audible quality is separate. */
#include "sb_pcm.h"
#include "sys.h"
#include "system.h"
#include "gfx.h"
#include "video_backend.h"
#include <dpmi.h>
#include <pc.h>
#include <sys/movedata.h>
#include <stdio.h>
#include <string.h>
static unsigned char block[DOS_SB_BLOCK_BYTES],readback[DOS_SB_BLOCK_BYTES];
static int wait_block(void)
{
    unsigned int start=g_ticks,guard=20000000;
    while(dos_sb_busy() && g_ticks-start<200 && --guard) {}
    return guard && !dos_sb_busy();
}
static int same_vector(__dpmi_paddr *a,__dpmi_paddr *b)
{ return a->offset32==b->offset32 && a->selector==b->selector; }
int main(void)
{
    unsigned int sizes[]={1,257,2048,31,511,2048,2048,2048};
    __dpmi_version_ret version;
    __dpmi_paddr before,after;
    unsigned int dsp=0,address=0,interrupts=0,start,elapsed;
    int dma_ok=1,copy_ok=1,video_ok=1,restore_ok=0,reinit_ok=0;
    puts("DOScraft SB1 DMA/IRQ7 diagnostic (not the game)");
    sys_init(); start=g_ticks;
    __dpmi_get_version(&version);
    __dpmi_get_protected_mode_interrupt_vector(version.master_pic+7,&before);
    unsigned char old_mask=inportb(0x21);
    int ok=dos_sb_init()==0;
    if(ok) { dsp=dos_sb_version(); address=dos_sb_dma_address(); }
    ok &= (dsp>>8)==1 && address && (address&65535)+sizeof(block)<=65536;
    video_init(); gfx_init();
    for(unsigned int i=0;i<sizeof(block);++i) block[i]=104+(i%32)*48/32;
    for(unsigned int i=0;i<sizeof(sizes)/sizeof(sizes[0]) && ok;++i) {
        unsigned int count=dos_sb_completions;
        ok=dos_sb_submit(block,sizes[i])==0;
        if(!ok) break;
        dosmemget(address,sizes[i],readback);
        copy_ok &= !memcmp(block,readback,sizes[i]);
        /* Keep Mode X/normal timer active while the transfer runs. */
        unsigned char *drawn=g_fb;
        gfx_clear(drawn,i*7); gfx_present(); gfx_wait_flip();
        video_ok &= !dos_video_verify(drawn,FB_PITCH);
        ok=wait_block() && dos_sb_completions==count+1;
        outportb(0x0c,0);
        unsigned int lo=inportb(0x03),hi=inportb(0x03);
        dma_ok &= (lo|(hi<<8))==65535; /* Terminal count, not auto-init. */
    }
    interrupts=dos_sb_completions;
    dos_sb_shutdown();
    __dpmi_get_protected_mode_interrupt_vector(version.master_pic+7,&after);
    restore_ok=same_vector(&before,&after) && inportb(0x21)==old_mask && !dos_sb_busy();
    if(ok) {
        reinit_ok=dos_sb_init()==0 && dos_sb_submit(block,257)==0 && wait_block() && dos_sb_completions==1;
        dos_sb_shutdown();
        __dpmi_get_protected_mode_interrupt_vector(version.master_pic+7,&after);
        reinit_ok &= same_vector(&before,&after) && inportb(0x21)==old_mask;
    }
    elapsed=g_ticks-start;
    dos_video_restore(); dos_system_shutdown();
    ok &= dma_ok && copy_ok && video_ok && restore_ok && reinit_ok && elapsed>0;
    FILE *report=fopen("C:\\SBPCM.TXT","wb");
    if(!report) return 1;
    int written=fprintf(report,"DOScraft SB1 single-cycle DMA diagnostic\nDSP_VERSION=%u.%02u\n"
        "OUTPUT_RATE=%u\nDMA_PHYSICAL=%u\nBLOCK_LIMIT=%u\nREAL_COMPLETIONS=%u\n"
        "CONVENTIONAL_COPY=%s\nDMA_TERMINAL_COUNT=%s\nMODEX_WITH_AUDIO_IRQ=%s\n"
        "VECTOR_MASK_RESTORATION=%s\nREINITIALIZATION=%s\nELAPSED_100HZ_TICKS=%u\n"
        "AUDIBLE_QUALITY=UNTESTED\nCONTINUOUS_PLAYBACK=UNIMPLEMENTED\nRESULT=%s\n",
        dsp>>8,dsp&255,DOS_SB_RATE,address,DOS_SB_BLOCK_BYTES,interrupts,
        copy_ok?"PASS":"FAIL",dma_ok?"PASS":"FAIL",video_ok?"PASS":"FAIL",
        restore_ok?"PASS":"FAIL",reinit_ok?"PASS":"FAIL",elapsed,ok?"PASS":"FAIL")>=0;
    if(fclose(report)) written=0;
    puts(ok && written ? "DOS SB1 DMA: PASS" : "DOS SB1 DMA: FAIL");
    return !(ok && written);
}
