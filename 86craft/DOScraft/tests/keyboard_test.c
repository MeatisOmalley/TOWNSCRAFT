/* The Python runner generates expected mappings and the pinned queue reference
 * in temporary storage. No Towns hardware or DOS guest execution here. */
#include <stdio.h>
#include <stdlib.h>
#include "sys.h"
#include "keyboard.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); exit(1); \
} } while (0)

struct Mapping { unsigned char extended, scan, key; };
#include "keyboard_cases.h"

extern volatile unsigned char reference_keyDown[128];
void reference_reset(void);
void reference_feed(int key, int release);
void reference_repeat(int key);
int reference_get_event(void);
void reference_flush(void);

static int irqEnabled=1, guardDepth, pending=-1;
static unsigned int saves, restores;

static void feed(unsigned int extended, unsigned int byte)
{
    int previous=irqEnabled;
    irqEnabled=0; /* Model a non-reentrant interrupt producer. */
    if (extended) dos_keyboard_scancode(0xE0);
    dos_keyboard_scancode((unsigned char)byte);
    irqEnabled=previous;
}

unsigned long dos_keyboard_irq_save(void)
{
    unsigned long state=(unsigned long)irqEnabled;
    CHECK(guardDepth==0);
    irqEnabled=0;
    ++guardDepth;
    ++saves;
    return state;
}

void dos_keyboard_irq_restore(unsigned long state)
{
    CHECK(guardDepth==1 && irqEnabled==0);
    --guardDepth;
    ++restores;
    irqEnabled=(int)state;
    if (irqEnabled && pending>=0) {
        int byte=pending;
        pending=-1;
        feed(0,(unsigned int)byte);
    }
}

static void clear(void)
{
    dos_keyboard_reset();
    reference_reset();
}

static void parity(void)
{
    unsigned int i;
    for (i=0; i<128; ++i) CHECK(g_keyDown[i]==reference_keyDown[i]);
}

static void sequence(const unsigned char *bytes, unsigned int count)
{
    unsigned int i;
    for (i=0; i<count; ++i) feed(0,bytes[i]);
}

static void mappings(void)
{
    unsigned int i, bank, byte, j;
    for (i=0; i<sizeof(expected)/sizeof(expected[0]); ++i) {
        const struct Mapping *m=&expected[i];
        clear();
        feed(m->extended,m->scan);
        reference_feed(m->key,0);
        parity();
        CHECK(key_get_event()==reference_get_event());
        CHECK(key_get_event()==-1);
        feed(m->extended,m->scan); /* typematic: another event */
        reference_repeat(m->key);
        parity();
        CHECK(key_get_event()==reference_get_event());
        feed(m->extended,m->scan|0x80);
        reference_feed(m->key,1);
        parity();
        CHECK(key_get_event()==-1);
        feed(m->extended,m->scan|0x80); /* duplicate release */
        parity();
    }
    /* Exhaustive isolated bytes in both banks, including unsupported bytes. */
    for (bank=0; bank<2; ++bank) for (byte=0; byte<256; ++byte) {
        int key=0;
        clear();
        for (j=0; j<sizeof(expected)/sizeof(expected[0]); ++j)
            if (expected[j].extended==bank && expected[j].scan==byte)
                key=expected[j].key;
        feed(bank,byte);
        CHECK(key_get_event()==(key ? key : -1));
        for (i=0; i<128; ++i)
            CHECK(g_keyDown[i]==(unsigned char)(key && (int)i==key));
    }
}

static void shared_modifiers(void)
{
    unsigned int ctrl, order, repeat, i;
    for (ctrl=0; ctrl<2; ++ctrl) for (order=0; order<2; ++order) {
        unsigned int scan[2]={ctrl ? 0x1D : 0x2A, ctrl ? 0x1D : 0x36};
        unsigned int bank[2]={0,ctrl};
        int key=ctrl ? KEY_CTRL : KEY_SHIFT;
        clear();
        feed(bank[0],scan[0]);
        feed(bank[1],scan[1]);
        for (repeat=0; repeat<300; ++repeat) feed(bank[1],scan[1]);
        CHECK(g_keyDown[key]==1); /* repetitions cannot overflow held state */
        feed(bank[order],scan[order]|0x80);
        CHECK(g_keyDown[key]==1);
        feed(bank[order],scan[order]|0x80);
        CHECK(g_keyDown[key]==1);
        feed(bank[1-order],scan[1-order]|0x80);
        CHECK(g_keyDown[key]==0);
        for (i=0; i<31; ++i) CHECK(key_get_event()==key);
        CHECK(key_get_event()==-1);
        /* An unmatched release must not release the other physical source. */
        feed(bank[0],scan[0]);
        feed(bank[1],scan[1]|0x80);
        CHECK(g_keyDown[key]==1);
        feed(bank[0],scan[0]|0x80);
        CHECK(g_keyDown[key]==0);
    }
}

static void special_sequences(void)
{
    static const unsigned char print[]={0xE0,0x2A,0xE0,0x37,0xE0,0xB7,0xE0,0xAA};
    static const unsigned char pause[]={0xE1,0x1D,0x45,0xE1,0x9D,0xC5};
    static const unsigned char ctrlPause[]={0xE0,0x46,0xE0,0xC6};
    static const unsigned char fake[]={0xE0,0x36,0xE0,0xB6,0xE0,0x1D,0xE0,0x9D};
    unsigned int shift, ctrl, length;
    for (shift=0; shift<2; ++shift) for (ctrl=0; ctrl<2; ++ctrl) {
        clear();
        feed(0,shift ? 0x36 : 0x2A);
        feed(ctrl,0x1D);
        key_flush();
        sequence(print,sizeof(print));
        sequence(pause,sizeof(pause));
        sequence(ctrlPause,sizeof(ctrlPause));
        CHECK(g_keyDown[KEY_SHIFT]==1 && g_keyDown[KEY_CTRL]==1);
        CHECK(key_get_event()==-1);
        feed(0,0x11);
        CHECK(key_get_event()==KEY_W);
        feed(0,shift ? 0xB6 : 0xAA);
        feed(ctrl,0x9D);
        CHECK(!g_keyDown[KEY_SHIFT] && !g_keyDown[KEY_CTRL]);
    }
    clear();
    sequence(fake,sizeof(fake));
    CHECK(!g_keyDown[KEY_SHIFT] && !g_keyDown[KEY_CTRL]);
    CHECK(key_get_event()==KEY_CTRL && key_get_event()==-1);
    /* All truncated Pause tails: mismatch is dropped, next input recovers. */
    for (length=1; length<sizeof(pause); ++length) {
        clear();
        feed(0,0x1D);
        key_flush();
        sequence(pause,length);
        feed(0,0x00);
        CHECK(g_keyDown[KEY_CTRL]==1 && key_get_event()==-1);
        feed(0,0x11);
        CHECK(key_get_event()==KEY_W);
        feed(0,0x9D);
        CHECK(!g_keyDown[KEY_CTRL]);
    }
    clear();
    feed(0,0xE0); feed(0,0xE0); feed(0,0x48);
    CHECK(key_get_event()==KEY_UP);
    feed(0,0xE1); feed(0,0xE0); feed(0,0x4B);
    CHECK(key_get_event()==KEY_LEFT);
    feed(0,0xE1); sequence(pause,sizeof(pause));
    CHECK(key_get_event()==-1);
    feed(0,0xE0); feed(0,0x00); feed(0,0x48);
    CHECK(key_get_event()==KEY_NUM_8); /* unsupported E0 consumes its prefix */
}

static void queue_and_reset(void)
{
    unsigned int i, cycle;
    clear();
    for (i=0; i<31; ++i) {
        feed(0,i&1 ? 0x11 : 0x1E);
        reference_feed(i&1 ? KEY_W : KEY_A,0);
    }
    feed(0,0x12); reference_feed(KEY_E,0); /* full: drop NEW, update held */
    parity();
    feed(0,0x92); reference_feed(KEY_E,1); /* breaks never enqueue */
    parity();
    for (i=0; i<31; ++i) {
        CHECK(key_get_event()==reference_get_event());
    }
    CHECK(key_get_event()==-1 && reference_get_event()==-1);
    for (cycle=0; cycle<80; ++cycle) {
        for (i=0; i<25; ++i) { feed(0,0x11); reference_feed(KEY_W,0); }
        for (i=0; i<17; ++i) CHECK(key_get_event()==reference_get_event());
        for (i=0; i<24; ++i) { feed(0,0x1E); reference_feed(KEY_A,0); }
        for (i=0; i<31; ++i) CHECK(key_get_event()==reference_get_event());
        CHECK(key_get_event()==-1 && reference_get_event()==-1);
        parity();
    }
    feed(0,0x11); reference_feed(KEY_W,0);
    feed(0,0xE0);
    key_flush(); reference_flush();
    parity();
    CHECK(key_get_event()==-1);
    feed(0,0x1C); CHECK(key_get_event()==KEY_NUM_RETURN); /* flush keeps prefix */
    dos_keyboard_reset();
    for (i=0; i<128; ++i) CHECK(!g_keyDown[i]);
    CHECK(key_get_event()==-1);
    feed(0,0xE0); dos_keyboard_reset(); feed(0,0x48);
    CHECK(key_get_event()==KEY_NUM_8);
    feed(0,0xE1); feed(0,0x1D); dos_keyboard_reset(); feed(0,0x11);
    CHECK(key_get_event()==KEY_W);
    feed(0,0x1D); feed(1,0x1D); dos_keyboard_reset();
    feed(0,0x1D); feed(0,0x9D);
    CHECK(!g_keyDown[KEY_CTRL]);
}

static void guards(void)
{
    unsigned int count;
    clear();
    count=saves;
    irqEnabled=0;
    CHECK(key_get_event()==-1 && irqEnabled==0);
    key_flush(); CHECK(irqEnabled==0);
    dos_keyboard_reset(); CHECK(irqEnabled==0);
    CHECK(saves==count+3 && saves==restores && guardDepth==0);
    irqEnabled=1;
    pending=0x11;
    CHECK(key_get_event()==-1); /* IRQ on restore belongs to the next read */
    CHECK(g_keyDown[KEY_W] && key_get_event()==KEY_W);
    feed(0,0x1E);
    pending=0x12;
    key_flush();
    CHECK(key_get_event()==KEY_E && key_get_event()==-1);
    pending=0x24;
    dos_keyboard_reset();
    CHECK(g_keyDown[KEY_J] && !g_keyDown[KEY_W]);
    CHECK(key_get_event()==KEY_J && key_get_event()==-1);
    CHECK(irqEnabled==1 && saves==restores && guardDepth==0);
}

static void trace(void)
{
    unsigned int step, random=0x12345678u;
    clear();
    for (step=0; step<80000; ++step) {
        unsigned int action, index;
        const struct Mapping *m;
        random=random*1664525u+1013904223u;
        action=random>>28;
        index=(random>>8)%(sizeof(expected)/sizeof(expected[0]));
        m=&expected[index];
        /* Queue parity uses one physical source per logical modifier. */
        if (m->key==KEY_CTRL || m->key==KEY_SHIFT) continue;
        if (action<8) {
            feed(m->extended,m->scan);
            reference_feed(m->key,0);
        } else if (action<12) {
            feed(m->extended,m->scan|0x80);
            reference_feed(m->key,1);
        } else if (action<15) {
            CHECK(key_get_event()==reference_get_event());
        } else {
            key_flush(); reference_flush();
        }
        parity();
    }
    while (1) {
        int key=key_get_event();
        CHECK(key==reference_get_event());
        if (key<0) break;
    }
}

int main(void)
{
    CHECK(sizeof(g_keyDown)==128);
    mappings();
    shared_modifiers();
    special_sequences();
    queue_and_reset();
    guards();
    trace();
    puts("PASS: AT mappings, modifiers, sequences, IRQ guards, pinned queue parity (80000 trace steps)");
    return 0;
}
