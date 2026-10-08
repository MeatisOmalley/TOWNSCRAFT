/* Low-level FM TOWNS hardware access. */
#ifndef HW_H
#define HW_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef short s16;
typedef int s32;

static inline void outb(u16 port,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port));}
static inline void outw(u16 port,u16 v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(port));}
static inline u8 inb(u16 port){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port));return v;}
static inline u16 inw(u16 port){u16 v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(port));return v;}
static inline void cli(void){__asm__ volatile("cli");}
static inline void sti(void){__asm__ volatile("sti");}
static inline void hlt(void){__asm__ volatile("hlt");}

#define VRAM ((volatile u8 *)0x80100000)  /* Single-page linear VRAM view */
#define VRAM_PITCH 1024

#endif
