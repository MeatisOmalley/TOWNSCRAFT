/* SB1 A220 I7 D1, unsigned mono, TC165 (~10989 Hz), command14 single-cycle.
 * References: Creative SB hardware guide ch2/3; DJGPP FAQ18.13. All costly
 * preparation is foreground-only. This bounded transport is not yet gapless
 * playback under long game frames; report that separately from DMA delivery. */
#include "sb_pcm.h"
#include "keyboard.h"
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdlib.h>
#include <sys/movedata.h>

#define BASE 0x220
#define IRQ_BIT 0x80
#define ALLOC_BYTES (2u * DOS_SB_BLOCK_BYTES)
#define POLL_LIMIT 65536u
static __dpmi_paddr old_vector;
static __dpmi_meminfo image_lock;
static int selector=-1,vector,installed,locked,ready,owns_dsp,exit_registered;
static unsigned char saved_irq_bit;
static unsigned int physical,dsp_version;
static volatile int busy;
volatile unsigned int dos_sb_completions,dos_sb_spurious;
unsigned short dos_sb_data_selector;
extern void dos_sb_irq_entry(void);
extern char image_end __asm__("end");

static int command(unsigned char value)
{
    for(unsigned int i=0;i<POLL_LIMIT;++i)
        if(!(inportb(BASE+12)&0x80)) { outportb(BASE+12,value); return 0; }
    return -1;
}
static int read_data(void)
{
    for(unsigned int i=0;i<POLL_LIMIT;++i)
        if(inportb(BASE+14)&0x80) return inportb(BASE+10);
    return -1;
}
static int reset_dsp(void)
{
    outportb(BASE+6,1);
    /* ISA I/O delays; not CPU-speed-dependent empty instruction loops. */
    for(int i=0;i<16;++i) inportb(0x80);
    outportb(BASE+6,0);
    return read_data()==0xaa ? 0 : -1;
}
static void restore_mask(void)
{
    /* Preserve other devices' current mask, notably resident timer/keyboard. */
    outportb(0x21,(inportb(0x21)&~IRQ_BIT)|saved_irq_bit);
}
void dos_sb_irq_body(void)
{
    outportb(0x20,0x0b); /* IRQ7 can be a spurious PIC edge; test in-service. */
    int real=inportb(0x20)&IRQ_BIT;
    outportb(0x20,0x0a); /* Restore IRR read mode for other clients. */
    if(!real) { ++dos_sb_spurious; return; } /* No EOI for spurious IRQ7. */
    inportb(BASE+14); /* Acknowledge DSP BEFORE PIC EOI. */
    if(busy) { busy=0; ++dos_sb_completions; }
    outportb(0x20,0x20);
}
void dos_sb_shutdown(void)
{
    unsigned long state=dos_keyboard_irq_save();
    if(installed || owns_dsp) {
        outportb(0x21,inportb(0x21)|IRQ_BIT);
        outportb(0x0a,5); /* Mask DMA1 before any buffer release. */
        /* Reset cancels a partial command or live single-cycle transfer even
         * if normal writes time out; DOS memory must never remain under DMA. */
        if(owns_dsp) {
            reset_dsp(); command(0xd3);
            inportb(BASE+14);
        }
        if(installed && __dpmi_set_protected_mode_interrupt_vector(vector,&old_vector))
            abort(); /* Do not free/unlock beneath a live vector. */
        installed=0; owns_dsp=0; ready=busy=0;
        restore_mask();
    }
    dos_keyboard_irq_restore(state);
    if(selector>=0) { __dpmi_free_dos_memory(selector); selector=-1; }
    if(locked) { __dpmi_unlock_linear_region(&image_lock); locked=0; }
    physical=dsp_version=0;
}
int dos_sb_init(void)
{
    __dpmi_version_ret version;
    __dpmi_paddr entry;
    unsigned long base,state;
    int segment,major,minor;
    if(ready || installed || selector>=0 || locked) return -1;
    if(__dpmi_get_version(&version)) return -1;
    vector=version.master_pic+7;
    if(__dpmi_get_protected_mode_interrupt_vector(vector,&old_vector)) return -1;
    dos_sb_data_selector=_my_ds();
    if(__dpmi_get_segment_base_address(dos_sb_data_selector,&base)) return -1;
    image_lock.address=base+0x1000;
    image_lock.size=(unsigned long)&image_end-0x1000;
    if(__dpmi_lock_linear_region(&image_lock)) return -1;
    locked=1;
    segment=__dpmi_allocate_dos_memory(ALLOC_BYTES/16,&selector);
    if(segment<0) { selector=-1; goto fail; }
    physical=(unsigned int)segment*16;
    if(physical+ALLOC_BYTES>0xa0000u) goto fail;
    /* At most 2047 bytes need skipping; 4096 allocated bytes cover either
     * placement. DMA1 cannot wrap a 64-KiB physical page inside this block. */
    if((physical&65535u)+DOS_SB_BLOCK_BYTES>65536u)
        physical=(physical+65535u)&~65535u;
    state=dos_keyboard_irq_save();
    saved_irq_bit=inportb(0x21)&IRQ_BIT;
    if(reset_dsp()) { dos_keyboard_irq_restore(state); goto fail; }
    owns_dsp=1;
    outportb(0x21,inportb(0x21)|IRQ_BIT);
    dos_keyboard_irq_restore(state);
    if(command(0xe1)) goto fail;
    major=read_data(); minor=read_data();
    if(major!=1 || minor<0) goto fail; /* No silent DSP2/Pro/16 substitution. */
    dsp_version=(major<<8)|minor;
    if(command(0x40) || command(165) || command(0xd1)) goto fail;
    if(!exit_registered) {
        if(atexit(dos_sb_shutdown)) goto fail;
        exit_registered=1;
    }
    entry.offset32=(unsigned long)dos_sb_irq_entry; entry.selector=_my_cs();
    state=dos_keyboard_irq_save();
    if(__dpmi_set_protected_mode_interrupt_vector(vector,&entry)) {
        dos_keyboard_irq_restore(state); goto fail;
    }
    installed=ready=1; busy=0; dos_sb_completions=dos_sb_spurious=0;
    outportb(0x21,inportb(0x21)&~IRQ_BIT);
    dos_keyboard_irq_restore(state);
    return 0;
fail:
    dos_sb_shutdown();
    return -1;
}
int dos_sb_submit(const unsigned char *samples,unsigned int bytes)
{
    if(!ready || !samples || !bytes || bytes>DOS_SB_BLOCK_BYTES) return -1;
    if(busy) return 1;
    dosmemput(samples,bytes,physical); /* Foreground only, owned idle buffer. */
    unsigned long state=dos_keyboard_irq_save();
    unsigned int count=bytes-1;
    outportb(0x0a,5); /* DMA1 masked, flip-flop cleared before each word. */
    outportb(0x0c,0); outportb(0x0b,0x49); /* Single, memory->device, increment. */
    outportb(0x02,physical&255); outportb(0x02,(physical>>8)&255);
    outportb(0x83,physical>>16);
    outportb(0x0c,0); outportb(0x03,count&255); outportb(0x03,count>>8);
    busy=1;
    outportb(0x0a,1);
    int failed=command(0x14) || command(count&255) || command(count>>8);
    dos_keyboard_irq_restore(state);
    if(failed) { dos_sb_shutdown(); return -1; }
    return 0;
}
int dos_sb_busy(void) { return busy; }
unsigned int dos_sb_version(void) { return dsp_version; }
unsigned int dos_sb_dma_address(void) { return physical; }
