/* System setup: IDT, interrupt controller, interval timer, keyboard, pad,
   memory. */
#include "sys.h"
#include "sound.h"
#include "gfx.h"

volatile u32 g_ticks;
volatile u32 g_vsyncCount;   /* Incremented by the VSYNC interrupt (isr.S) */
volatile u8 g_keyDown[128];
u32 g_ramMB;

#define KEYQ_LEN 32
static volatile u8 keyQ[KEYQ_LEN];
static volatile u32 keyQHead,keyQTail;
static u8 kbdFirstByte;

struct IdtEntry
{
	u16 offsetLow;
	u16 selector;
	u8 zero;
	u8 type;
	u16 offsetHigh;
} __attribute__((packed));

static struct IdtEntry idt[64];

extern void exc0(void),exc1(void),exc2(void),exc3(void),exc4(void),exc5(void),exc6(void),exc7(void),
	exc8(void),exc9(void),exc10(void),exc11(void),exc12(void),exc13(void),exc14(void),exc15(void),
	exc16(void),exc17(void),exc18(void),exc19(void);
extern void irq_timer(void),irq_vsync(void),irq_master_spurious(void),irq_slave_spurious(void);

static void set_gate(int n,void (*fn)(void))
{
	u32 a=(u32)fn;
	idt[n].offsetLow=a&0xFFFF;
	idt[n].offsetHigh=a>>16;
	idt[n].selector=0x08;
	idt[n].zero=0;
	idt[n].type=0x8E;
}

static void idt_init(void)
{
	static void (*const exc[20])(void)=
	{
		exc0,exc1,exc2,exc3,exc4,exc5,exc6,exc7,exc8,exc9,
		exc10,exc11,exc12,exc13,exc14,exc15,exc16,exc17,exc18,exc19
	};
	int i;
	struct { u16 limit; u32 base; } __attribute__((packed)) idtr;
	for(i=0; i<64; ++i)
	{
		set_gate(i,exc13);
	}
	for(i=0; i<20; ++i)
	{
		set_gate(i,exc[i]);
	}
	for(i=0; i<8; ++i)
	{
		set_gate(0x20+i,irq_master_spurious);
		set_gate(0x28+i,irq_slave_spurious);
	}
	set_gate(0x20,irq_timer);
	set_gate(0x28+3,irq_vsync);   /* IRQ 11 */
	idtr.limit=sizeof(idt)-1;
	idtr.base=(u32)idt;
	__asm__ volatile("lidt %0"::"m"(idtr));
}

static void io_delay(void)
{
	outb(0x6C,0);  /* 1us wait register on later models; harmless write otherwise */
}

static void pic_init(void)
{
	/* FM TOWNS PICs must be level-triggered.  Secondary is on primary IR7. */
	outb(0x00,0x19); io_delay();
	outb(0x02,0x20); io_delay();  /* Vector base 20h */
	outb(0x02,0x80); io_delay();  /* Secondary on IR7 */
	outb(0x02,0x1D); io_delay();
	outb(0x10,0x19); io_delay();
	outb(0x12,0x28); io_delay();  /* Vector base 28h */
	outb(0x12,0x07); io_delay();
	outb(0x12,0x09); io_delay();
	outb(0x02,0x7E);   /* IRQ0 (timer) and IR7 (secondary controller) */
	outb(0x12,0xF7);   /* IRQ11 (VSYNC) */
}

static void pit_init(void)
{
	/* Timer input is 307.2KHz.  Channel 0, mode 3 (square wave). */
	u16 div=307200/TICKS_PER_SEC;
	outb(0x46,0x36);
	outb(0x40,div&0xFF);
	outb(0x40,div>>8);
	outb(0x60,0x81);   /* Clear timer 0 output flag, enable timer 0 interrupt */
}

static void kbd_poll(void)
{
	int guard=16;
	while((inb(0x602)&1) && 0<guard--)
	{
		u8 d=inb(0x600);
		if(d&0x80)
		{
			kbdFirstByte=d;
		}
		else if(kbdFirstByte)
		{
			int code=d&0x7F;
			if(kbdFirstByte&0x10)
			{
				g_keyDown[code]=0;
			}
			else
			{
				u32 next=(keyQHead+1)%KEYQ_LEN;
				g_keyDown[code]=1;
				if(next!=keyQTail)
				{
					keyQ[keyQHead]=code;
					keyQHead=next;
				}
			}
			kbdFirstByte=0;
		}
	}
}

#define PROF_SAMPLES 4096
u32 g_profSamples[PROF_SAMPLES];
u32 g_profCount;
int g_profEnable;

void timer_isr(u32 eip)
{
	outb(0x60,0x81);   /* Acknowledge timer 0 */
	++g_ticks;
	if(g_profEnable && g_profCount<PROF_SAMPLES)
	{
		g_profSamples[g_profCount++]=eip;
	}
	kbd_poll();
	sound_tick();      /* Music sequencer */
	outb(0x00,0x20);   /* EOI */
}

int key_get_event(void)
{
	int k=-1;
	cli();
	if(keyQHead!=keyQTail)
	{
		k=keyQ[keyQTail];
		keyQTail=(keyQTail+1)%KEYQ_LEN;
	}
	sti();
	return k;
}

void key_flush(void)
{
	cli();
	keyQTail=keyQHead;
	sti();
}

u32 pad_read(void)
{
	u8 d=inb(0x4D0);
	u32 r=0;
	if(0==(d&0x01)) r|=PAD_UP;
	if(0==(d&0x02)) r|=PAD_DOWN;
	if(0==(d&0x04)) r|=PAD_LEFT;
	if(0==(d&0x08)) r|=PAD_RIGHT;
	if(0==(d&0x10)) r|=PAD_A;
	if(0==(d&0x20)) r|=PAD_B;
	/* RUN/SELECT are encoded as impossible direction combinations */
	if((r&(PAD_LEFT|PAD_RIGHT))==(PAD_LEFT|PAD_RIGHT))
	{
		r&=~(PAD_LEFT|PAD_RIGHT);
		r|=PAD_RUN;
	}
	if((r&(PAD_UP|PAD_DOWN))==(PAD_UP|PAD_DOWN))
	{
		r&=~(PAD_UP|PAD_DOWN);
		r|=PAD_SELECT;
	}
	return r;
}

/* The FM TOWNS mouse (MSX protocol) sends its motion as four nibbles, X
   high, X low, Y high, Y low, stepped by toggling the COM line of its port
   (0x4D6 bit 5 for port B; bits 2-3 are port B's trigger outputs and must
   stay high to read the buttons).  The counts are the displacement since
   the last read, positive for left and up.  Without a mouse the port reads
   all ones, which decodes as (-1,-1): that is ignored. */
static int mouse_nibble(u8 out)
{
	int i;
	u8 d=0;
	outb(0x4D6,out);
	for(i=0; i<8; ++i)   /* Settle time (each read is about a microsecond) */
	{
		d=inb(0x4D2);
	}
	return d;
}

int mouse_read(int *dx,int *dy)
{
	int xh=mouse_nibble(0x2F),xl=mouse_nibble(0x0F);
	int yh=mouse_nibble(0x2F),yl=mouse_nibble(0x0F);
	int buttons=0;
	*dx=0;
	*dy=0;
	if(0x0F!=(xh&xl&yh&yl&0x0F))
	{
		*dx=-(s8)(((xh&15)<<4)|(xl&15));
		*dy=-(s8)(((yh&15)<<4)|(yl&15));
	}
	if(0==(xh&0x10)) buttons|=MOUSE_L;
	if(0==(xh&0x20)) buttons|=MOUSE_R;
	return buttons;
}

/* Heap */
extern u8 __bss_end[];
static u32 lowPtr,lowEnd,highPtr,highEnd;

void *heap_alloc_low(u32 size)
{
	u32 p=(lowPtr+15)&~15;
	if(p+size>lowEnd)
	{
		return heap_alloc_high(size);
	}
	lowPtr=p+size;
	return (void *)p;
}

void *heap_alloc_high(u32 size)
{
	u32 p=(highPtr+15)&~15;
	if(p+size>highEnd)
	{
		fatal("Out of memory");
	}
	highPtr=p+size;
	return (void *)p;
}

u32 heap_high_free(void)
{
	return highEnd-((highPtr+15)&~15);
}

u32 heap_low_free(void)
{
	return lowEnd-((lowPtr+15)&~15);
}

void sys_init(void)
{
	cli();
	g_ramMB=inb(0x5E8)&0x7F;
	if(g_ramMB<2)
	{
		g_ramMB=2;
	}
	lowPtr=(u32)__bss_end;
	lowEnd=0xB8000;   /* Leave 32KB for the stack below C0000h */
	highPtr=0x100000;
	highEnd=g_ramMB*0x100000;

	idt_init();
	pic_init();
	pit_init();
	outb(0x604,0);     /* Keyboard IRQ off.  Polled from the timer. */
	outb(0x4D6,0x0F);  /* Game port trigger lines high so buttons can be read */
	while(inb(0x602)&1)
	{
		inb(0x600);
	}
	sti();
}

/* Exceptions: show a crash screen with registers.  Useful for debugging. */
struct ExcFrame
{
	u32 edi,esi,ebp,esp,ebx,edx,ecx,eax;
	u32 num,err,eip,cs,eflags;
};

static void hexstr(char *b,u32 v)
{
	int i;
	for(i=0; i<8; ++i)
	{
		b[i]="0123456789ABCDEF"[(v>>(28-i*4))&15];
	}
	b[8]=0;
}

void exception_handler(struct ExcFrame *f)
{
	static const char *const names[]={"EAX","EBX","ECX","EDX","ESI","EDI","EBP","EIP","ERR","NUM"};
	u32 vals[10];
	char buf[16];
	int i;
	vals[0]=f->eax; vals[1]=f->ebx; vals[2]=f->ecx; vals[3]=f->edx;
	vals[4]=f->esi; vals[5]=f->edi; vals[6]=f->ebp; vals[7]=f->eip;
	vals[8]=f->err; vals[9]=f->num;
	gfx_clear(g_fb,0x9F);
	gfx_text(g_fb,8,8,"CPU EXCEPTION",0x0F);
	for(i=0; i<10; ++i)
	{
		gfx_text(g_fb,8,24+i*10,names[i],0x0F);
		hexstr(buf,vals[i]);
		gfx_text(g_fb,48,24+i*10,buf,0x0F);
	}
	gfx_present();
	for(;;)
	{
		cli();
		hlt();
	}
}

void fatal(const char *msg)
{
	cli();
	gfx_clear(g_fb,0x9F);
	gfx_text(g_fb,8,8,"FATAL ERROR",0x0F);
	gfx_text(g_fb,8,24,msg,0x0F);
	gfx_present();
	for(;;)
	{
		cli();
		hlt();
	}
}
