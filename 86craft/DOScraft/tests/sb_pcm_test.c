/* Only bounded fake DPMI/ports. Native tests never execute IRQ assembly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <dpmi.h>
static void test_abort(void);
static int test_atexit(void (*fn)(void));
#define abort test_abort
#define atexit test_atexit
#include "sb_pcm.c"
#undef abort
#undef atexit
char image_end;
static const char *fault;
static int interrupt_state=1,allocated,locked_regions,vector_live,frees,unlocks;
static int segment_value=0x2ff0,restore_fails,abort_expected;
static int command_count,fail_command=-1,dsp_major=1;
static unsigned char pic_mask=0xff,pic_isr=0x80;
static unsigned char response[3];
static int response_count,response_pos;
static unsigned int copy_bytes,copy_address;
static int dma_mask=1,in_irq;
static struct { unsigned short port; unsigned char value; } log_rows[256];
static int log_count;
static jmp_buf abort_jump;
static void check(int ok,const char *why)
{ if(!ok) { fprintf(stderr,"FAIL: %s\n",why); exit(1); } }
static int failing(const char *name) { return fault && !strcmp(fault,name); }
static void test_abort(void)
{ check(abort_expected,"unexpected abort"); longjmp(abort_jump,1); }
static int test_atexit(void (*fn)(void))
{ check(fn==dos_sb_shutdown,"cleanup registration"); return failing("atexit"); }
unsigned short _my_ds(void) { return 0x17; }
unsigned short _my_cs(void) { return 0x0f; }
void dos_sb_irq_entry(void) {}
unsigned long dos_keyboard_irq_save(void)
{ int old=interrupt_state; interrupt_state=0; return old; }
void dos_keyboard_irq_restore(unsigned long value) { interrupt_state=(int)value; }
int __dpmi_get_version(__dpmi_version_ret *v)
{ v->master_pic=0x08; return failing("version"); }
int __dpmi_get_protected_mode_interrupt_vector(int n,__dpmi_paddr *p)
{ check(n==15,"IRQ7 vector"); p->offset32=0x9876; p->selector=0x20; return failing("get_vector"); }
int __dpmi_set_protected_mode_interrupt_vector(int n,__dpmi_paddr *p)
{
    check(n==15 && !interrupt_state,"vector update IRQ guard");
    if(p->offset32==0x9876) {
        if(restore_fails) return -1;
        vector_live=0;
    } else {
        check(locked_regions && allocated && (pic_mask&128),"resident masked installation");
        if(failing("install")) return -1;
        vector_live=1;
    }
    return 0;
}
int __dpmi_get_segment_base_address(int s,unsigned long *base)
{ check(s==0x17,"data selector"); *base=0x400000; return failing("segment"); }
int __dpmi_lock_linear_region(__dpmi_meminfo *m)
{ check(m->address==0x401000 && m->size,"image lock"); if(failing("lock")) return -1; ++locked_regions; return 0; }
int __dpmi_unlock_linear_region(__dpmi_meminfo *m)
{ (void)m; check(!vector_live,"unlock never beneath live vector"); --locked_regions; ++unlocks; return 0; }
int __dpmi_allocate_dos_memory(int paragraphs,int *s)
{
    check(paragraphs==256,"small conventional allocation");
    if(failing("allocate")) { *s=12; return -1; }
    allocated=1; *s=0x37;
    return failing("address") ? 0x9ff0 : segment_value;
}
int __dpmi_free_dos_memory(int s)
{ check(s==0x37 && allocated && !vector_live && dma_mask,"stop DMA before buffer free"); allocated=0; ++frees; return 0; }
void dosmemput(const void *bytes,size_t count,unsigned long address)
{
    check(ready && !busy && allocated && bytes && count<=2048,"foreground idle copy");
    check(address>=(unsigned)segment_value*16 && address+count<=(unsigned)segment_value*16+4096,
        "copy within allocation");
    check((address&65535)+count<=65536,"no DMA page wrap");
    copy_bytes=(unsigned int)count; copy_address=address;
}
unsigned char inportb(unsigned short port)
{
    if(port==0x21) return pic_mask;
    if(port==0x20) return pic_isr;
    if(port==0x80) return 0;
    if(port==BASE+12) return command_count==fail_command || failing("command") ? 0x80 : 0;
    if(port==BASE+14) {
        if(in_irq) {
            check(log_count>0 && log_rows[log_count-1].port==0x20 && log_rows[log_count-1].value==0x0a,
                  "DSP ack after spurious test, before EOI");
        }
        return response_pos<response_count ? 0x80 : 0;
    }
    if(port==BASE+10) { check(response_pos<response_count,"DSP read ready"); return response[response_pos++]; }
    check(0,"unknown input port"); return 0;
}
void outportb(unsigned short port,unsigned char value)
{
    check(log_count<256,"bounded writes");
    log_rows[log_count].port=port; log_rows[log_count++].value=value;
    if(port==0x21) { pic_mask=value; return; }
    if(port==0x0a) { check(value==1 || value==5,"DMA1 only"); dma_mask=value==5; return; }
    if(port==0x0c || port==0x0b || port==0x02 || port==0x03 || port==0x83) {
        check(!interrupt_state && dma_mask,"DMA register guarded and masked"); return;
    }
    if(port==0x20) return;
    if(port==BASE+6) {
        if(!value) { response[0]=0xaa; response_pos=0; response_count=failing("reset") ? 0 : 1; }
        return;
    }
    if(port==BASE+12) {
        ++command_count;
        if(value==0xe1) {
            response[0]=failing("dsp_version") ? 2 : dsp_major; response[1]=5;
            response_pos=0; response_count=2;
        }
        return;
    }
    check(0,"unknown output port");
}
static void clean(void)
{
    dos_sb_shutdown();
    check(!ready && !busy && !allocated && !vector_live && !locked_regions,"complete unwind");
    check(!dos_sb_version() && !dos_sb_dma_address(),"cleared public state");
}
static void start(void)
{
    check(!dos_sb_init(),"initialization");
    check(dos_sb_version()==0x105 && !(pic_mask&128),"DSP1 and IRQ unmasked");
    check(dos_sb_init()==-1,"live reinitialization refused");
}
static void lifecycle(void)
{
    unsigned char data[2048]={128};
    int original=interrupt_state;
    start(); check(interrupt_state==original,"init preserves IF");
    check(dos_sb_submit(NULL,1)==-1 && dos_sb_submit(data,0)==-1 && dos_sb_submit(data,2049)==-1,
          "invalid submissions");
    for(unsigned int bytes=1;bytes<=2048;bytes*=2) {
        log_count=0;
        check(!dos_sb_submit(data,bytes) && dos_sb_busy(),"accepted block");
        check(copy_bytes==bytes && copy_address==physical,"copy exact bytes/address");
        check(dos_sb_submit(data,bytes)==1,"busy buffer ownership");
        check(log_rows[1].port==0x0c && log_rows[2].port==0x0b && log_rows[2].value==0x49,
              "single-cycle DMA mode");
        check(log_rows[7].port==0x03 && log_rows[7].value==((bytes-1)&255) &&
              log_rows[8].port==0x03 && log_rows[8].value==((bytes-1)>>8),"count minus one LE");
        check(log_rows[10].port==BASE+12 && log_rows[10].value==0x14 &&
              log_rows[11].value==((bytes-1)&255) && log_rows[12].value==((bytes-1)>>8),
              "DSP1 single-cycle command and count, never auto-init");
        unsigned int before=dos_sb_completions;
        pic_isr=0; log_count=0; in_irq=1; dos_sb_irq_body(); in_irq=0;
        check(busy && dos_sb_completions==before && dos_sb_spurious && log_count==2,"spurious no DSP ack/EOI");
        pic_isr=128; log_count=0; in_irq=1; dos_sb_irq_body(); in_irq=0;
        check(!busy && dos_sb_completions==before+1,"real completion");
        check(log_rows[2].port==0x20 && log_rows[2].value==0x20,"PIC EOI after DSP ack");
    }
    /* Preserve other clients' mask changes, restore only our original bit. */
    pic_mask=0x18; log_count=0; clean();
    check(pic_mask==0x98 && interrupt_state==original,"mask/IF restoration");
    log_count=0; start(); log_count=0; clean();
}
int main(int argc,char **argv)
{
    check(argc>=2,"named test");
    if(!strcmp(argv[1],"lifecycle")) { interrupt_state=atoi(argv[2]); lifecycle(); }
    else if(!strcmp(argv[1],"boundaries")) {
        for(segment_value=0x2f00;segment_value<=0x3100;++segment_value) {
            log_count=0; start();
            check((physical&65535)+2048<=65536 && physical>=(unsigned)segment_value*16 &&
                physical+2048<=(unsigned)segment_value*16+4096,"every paragraph boundary placement");
            clean();
        }
    } else if(!strcmp(argv[1],"failure")) {
        check(argc==4,"failure arguments"); fault=argv[2]; interrupt_state=atoi(argv[3]);
        int original=interrupt_state;
        check(dos_sb_init()==-1,"injected init failure");
        check(!allocated && !vector_live && !locked_regions && pic_mask==255 && interrupt_state==original,
              "failure unwind mask/IF/memory");
        fault=NULL; log_count=0; start(); clean();
    } else if(!strcmp(argv[1],"start_failure")) {
        unsigned char data[2]={128,129}; start();
        fail_command=command_count+atoi(argv[2]); log_count=0;
        check(dos_sb_submit(data,2)==-1 && !allocated && !vector_live && !busy,"partial DSP command unwind");
        fail_command=-1; log_count=0; start(); clean();
    } else if(!strcmp(argv[1],"restore_failure")) {
        start(); restore_fails=abort_expected=1;
        if(!setjmp(abort_jump)) { dos_sb_shutdown(); check(0,"restore failure must abort"); }
        check(vector_live && allocated && locked_regions && !frees && !unlocks && dma_mask,
              "live IRQ memory retained after restore failure");
    } else check(0,"unknown case");
    puts("PASS: SB1 transport"); return 0;
}
