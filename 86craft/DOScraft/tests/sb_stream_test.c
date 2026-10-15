#define DOS_SB_STREAM
#define main legacy_test_main
#include "sb_pcm_test.c"
#undef main
static unsigned int fills;
static void fill(unsigned char *out,unsigned int bytes)
{ check(bytes==512,"bounded stream callback");memset(out,128,bytes);++fills; }
int main(void)
{
    start();log_count=0;
    check(!dos_sb_stream_start(fill) && busy && fills==2,"two primed DMA blocks");
    for(unsigned int i=0;i<100;++i) {
        log_count=0;in_irq=1;interrupt_state=0;dos_sb_irq_body();in_irq=0;interrupt_state=1;
        check(busy && dos_sb_completions==i+1 && fills==i+3,"continuous refill through long foreground gaps");
        check(copy_address==physical+(i%2)*512 && copy_bytes==512,"only completed block refilled");
        check(log_rows[log_count-1].port==0x20 && log_rows[log_count-1].value==0x20,"EOI after DMA rearm/refill");
    }
    log_count=0;pic_isr=0;in_irq=1;dos_sb_irq_body();in_irq=0;
    check(fills==102 && dos_sb_spurious==1,"spurious IRQ cannot advance audio");
    pic_isr=128;log_count=0;fail_command=command_count;in_irq=1;interrupt_state=0;
    dos_sb_irq_body();in_irq=0;interrupt_state=1;
    check(dos_sb_stream_faults==1 && !busy && !stream_fill && dma_mask,"bounded DSP failure masks DMA");
    fail_command=-1;log_count=0;clean();
    puts("PASS: continuous SB1 DMA ping-pong, refill ownership, spurious IRQ and failure cleanup");return 0;
}
