#ifndef SYS_H
#define SYS_H
#include "common.h"

#define TICKS_PER_SEC 100
extern volatile u32 g_ticks;
extern volatile u32 g_vsyncCount;   /* Vertical syncs since start */

void sys_init(void);

/* Keyboard.  Key codes are FM TOWNS JIS key codes (see KEY_* below). */
extern volatile u8 g_keyDown[128];
int key_get_event(void);      /* Next pressed key code, or -1. */
void key_flush(void);

/* Game pad on port A: bits set when pressed. */
enum
{
	PAD_UP=1,PAD_DOWN=2,PAD_LEFT=4,PAD_RIGHT=8,PAD_A=16,PAD_B=32,PAD_RUN=64,PAD_SELECT=128
};
u32 pad_read(void);

/* Mouse on game port B.  Returns MOUSE_L|MOUSE_R for the buttons held and
   the motion since the last call (right and down positive). */
enum
{
	MOUSE_L=1,MOUSE_R=2
};
int mouse_read(int *dx,int *dy);

/* Memory */
extern u32 g_ramMB;
void *heap_alloc_low(u32 size);   /* Conventional memory (below 640KB+) */
void *heap_alloc_high(u32 size);  /* Above 1MB */
u32 heap_high_free(void);
u32 heap_low_free(void);

void fatal(const char *msg);

enum
{
	KEY_ESC=0x01,KEY_1=0x02,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9,KEY_0,
	KEY_MINUS=0x0C,KEY_BACKSPACE=0x0F,KEY_TAB=0x10,
	KEY_Q=0x11,KEY_W,KEY_E,KEY_R,KEY_T,KEY_Y,KEY_U,KEY_I,KEY_O,KEY_P,
	KEY_RETURN=0x1D,
	KEY_A=0x1E,KEY_S,KEY_D,KEY_F,KEY_G,KEY_H,KEY_J,KEY_K,KEY_L,
	KEY_Z=0x2A,KEY_X,KEY_C,KEY_V,KEY_B,KEY_N,KEY_M,KEY_COMMA,KEY_DOT,KEY_SLASH,
	KEY_SPACE=0x35,
	KEY_NUM_8=0x3B,KEY_NUM_4=0x3E,KEY_NUM_5=0x3F,KEY_NUM_6=0x40,KEY_NUM_2=0x43,
	KEY_NUM_RETURN=0x45,
	KEY_INSERT=0x48,KEY_DELETE=0x4B,KEY_UP=0x4D,KEY_HOME=0x4E,KEY_LEFT=0x4F,KEY_DOWN=0x50,KEY_RIGHT=0x51,
	KEY_CTRL=0x52,KEY_SHIFT=0x53,
	KEY_PF1=0x5D,KEY_PF2,KEY_PF3,KEY_PF4,KEY_PF5,KEY_PF6,KEY_PF7,KEY_PF8,KEY_PF9,KEY_PF10,
	KEY_EXECUTE=0x73,
};

#endif
