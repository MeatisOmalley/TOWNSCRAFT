#include "keyboard.h"
#include "sys.h" /* Generated DOS declarations; KEY_* remain pinned Towns values. */

#define KEYQ_LEN 32
volatile unsigned char g_keyDown[128];
static volatile unsigned char keyQ[KEYQ_LEN];
static volatile unsigned int keyQHead, keyQTail;

/* Zero means unsupported. Prefix bytes never index these bounded tables. */
static const unsigned char keys[2][128] = {
    {
        [0x01]=KEY_ESC,
        [0x02]=KEY_1, [0x03]=KEY_2, [0x04]=KEY_3, [0x05]=KEY_4,
        [0x06]=KEY_5, [0x07]=KEY_6, [0x08]=KEY_7, [0x09]=KEY_8,
        [0x0A]=KEY_9, [0x0B]=KEY_0, [0x0C]=KEY_MINUS,
        [0x0E]=KEY_BACKSPACE, [0x0F]=KEY_TAB,
        [0x10]=KEY_Q, [0x11]=KEY_W, [0x12]=KEY_E, [0x13]=KEY_R,
        [0x14]=KEY_T, [0x15]=KEY_Y, [0x16]=KEY_U, [0x17]=KEY_I,
        [0x18]=KEY_O, [0x19]=KEY_P, [0x1C]=KEY_RETURN,
        [0x1D]=KEY_CTRL,
        [0x1E]=KEY_A, [0x1F]=KEY_S, [0x20]=KEY_D, [0x21]=KEY_F,
        [0x22]=KEY_G, [0x23]=KEY_H, [0x24]=KEY_J, [0x25]=KEY_K,
        [0x26]=KEY_L, [0x2A]=KEY_SHIFT,
        [0x2C]=KEY_Z, [0x2D]=KEY_X, [0x2E]=KEY_C, [0x2F]=KEY_V,
        [0x30]=KEY_B, [0x31]=KEY_N, [0x32]=KEY_M,
        [0x33]=KEY_COMMA, [0x34]=KEY_DOT, [0x35]=KEY_SLASH,
        [0x36]=KEY_SHIFT, [0x39]=KEY_SPACE,
        [0x3B]=KEY_PF1, [0x3C]=KEY_PF2, [0x3D]=KEY_PF3,
        [0x3E]=KEY_PF4, [0x3F]=KEY_PF5, [0x40]=KEY_PF6,
        [0x41]=KEY_PF7, [0x42]=KEY_PF8, [0x43]=KEY_PF9,
        [0x44]=KEY_PF10,
        [0x48]=KEY_NUM_8, [0x4B]=KEY_NUM_4, [0x4C]=KEY_NUM_5,
        [0x4D]=KEY_NUM_6, [0x50]=KEY_NUM_2
    },
    {
        [0x1C]=KEY_NUM_RETURN, [0x1D]=KEY_CTRL,
        [0x47]=KEY_HOME, [0x48]=KEY_UP, [0x4B]=KEY_LEFT,
        [0x4D]=KEY_RIGHT, [0x50]=KEY_DOWN,
        [0x52]=KEY_INSERT, [0x53]=KEY_DELETE
    }
};

/* Physical sources are distinct even when they map to one Towns key. A
 * typematic make queues another press but does not increment heldCount. */
static unsigned char physicalDown[2][128], heldCount[128];
static unsigned char extended, pauseIndex;
static const unsigned char pauseTail[5] = {0x1D,0x45,0xE1,0x9D,0xC5};

void dos_keyboard_reset(void)
{
    unsigned long state = dos_keyboard_irq_save();
    unsigned int i;
    for (i=0; i<128; ++i) {
        g_keyDown[i]=0;
        heldCount[i]=0;
        physicalDown[0][i]=physicalDown[1][i]=0;
    }
    keyQHead=keyQTail=0;
    extended=pauseIndex=0;
    dos_keyboard_irq_restore(state);
}

void dos_keyboard_scancode(unsigned char byte)
{
    unsigned int bank, scan, code, next;
    if (pauseIndex) {
        if (byte==pauseTail[pauseIndex-1]) {
            if (++pauseIndex==6) pauseIndex=0;
            return;
        }
        pauseIndex=0;
        extended=0;
        if (byte==0xE1) pauseIndex=1;
        else if (byte==0xE0) extended=1;
        return;
    }
    if (byte==0xE1) {
        extended=0;
        pauseIndex=1;
        return;
    }
    if (byte==0xE0) {
        extended=1;
        return;
    }
    bank=extended;
    extended=0;
    scan=byte&0x7F;
    code=keys[bank][scan];
    if (!code) return;
    if (byte&0x80) {
        if (physicalDown[bank][scan]) {
            physicalDown[bank][scan]=0;
            --heldCount[code];
        }
        g_keyDown[code]=(heldCount[code]!=0);
        return;
    }
    if (!physicalDown[bank][scan]) {
        physicalDown[bank][scan]=1;
        ++heldCount[code];
    }
    g_keyDown[code]=1;
    next=(keyQHead+1)%KEYQ_LEN;
    if (next!=keyQTail) {
        keyQ[keyQHead]=(unsigned char)code;
        keyQHead=next;
    }
}

int key_get_event(void)
{
    int key=-1;
    unsigned long state=dos_keyboard_irq_save();
    if (keyQHead!=keyQTail) {
        key=keyQ[keyQTail];
        keyQTail=(keyQTail+1)%KEYQ_LEN;
    }
    dos_keyboard_irq_restore(state);
    return key;
}

void key_flush(void)
{
    unsigned long state=dos_keyboard_irq_save();
    keyQTail=keyQHead;
    dos_keyboard_irq_restore(state);
}
