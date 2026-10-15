/* N64 input for DOScraft's unchanged Towns input API (Q5).
 *
 * Official interface: N64 Mouse + Randnet keyboard. The keyboard needs a
 * Joybus driver and an emulator with the device (ares fork, in progress), so
 * this file currently provides the mouse and the controller fallback.
 *
 * - N64 Mouse (any port): motion -> mouse_read deltas, left/right buttons ->
 *   MOUSE_L/MOUSE_R, exactly as the DOS mouse driver feeds the game.
 * - Controller fallback (Turok-style, OPEN_QUESTIONS Q5): the analog stick
 *   looks (as mouse motion); buttons become the AT set-1 scancodes DOScraft's
 *   keyboard.c already decodes, so menus, inventory and gameplay keys work
 *   unchanged:
 *     C-up/down/left/right = W S A D    A = Space (jump, menu select)
 *     B = Ctrl (sprint)                 Z = J (break/attack)
 *     R = E (use/place)                 L = Tab (inventory)
 *     D-up = C (crafting)               D-left/right = , . (hotbar)
 *     D-down = Q (discard in inventory) Start = Esc (pause/menus)
 * Input is sampled once per presented frame. */
#include <libdragon.h>
#include "sys.h"
#include "keyboard.h"

/* AT set-1 make codes (break = make | 0x80). */
enum { SC_ESC = 0x01, SC_TAB = 0x0F, SC_Q = 0x10, SC_W = 0x11, SC_E = 0x12,
       SC_CTRL = 0x1D, SC_A = 0x1E, SC_S = 0x1F, SC_D = 0x20, SC_J = 0x24,
       SC_C = 0x2E, SC_COMMA = 0x33, SC_DOT = 0x34, SC_SPACE = 0x39 };

static const struct { uint16_t mask; uint8_t code; } buttons[] = {
    { 1u << 11, SC_W },     /* C-up    */
    { 1u << 10, SC_S },     /* C-down  */
    { 1u << 9,  SC_A },     /* C-left  */
    { 1u << 8,  SC_D },     /* C-right */
    { 1u << 15, SC_SPACE }, /* A       */
    { 1u << 14, SC_CTRL },  /* B       */
    { 1u << 13, SC_J },     /* Z       */
    { 1u << 4,  SC_E },     /* R       */
    { 1u << 5,  SC_TAB },   /* L       */
    { 1u << 3,  SC_C },     /* D-up    */
    { 1u << 1,  SC_COMMA }, /* D-left  */
    { 1u << 0,  SC_DOT },   /* D-right */
    { 1u << 2,  SC_Q },     /* D-down  */
    { 1u << 12, SC_ESC },   /* Start   */
};
#define NBUTTONS (sizeof(buttons) / sizeof(buttons[0]))

static uint16_t held;               /* controller buttons last frame */
static int mouse_dx, mouse_dy, mouse_buttons;

/* Test input, written by tools/ares_drive.py through ares's GDB server and
 * consumed once per frame: scancodes (ring), mouse motion and buttons. The
 * frame counter lets the driver wait for frames. Read uncached: the debugger
 * writes RDRAM directly. */
typedef struct {
    uint32_t magic;             /* 'T64I' */
    uint32_t frame;             /* presented frames */
    uint32_t head, tail;        /* codes[tail..head) pending */
    int32_t mouse_dx, mouse_dy;
    uint32_t mouse_buttons;     /* OR'd into the real buttons */
    uint8_t codes[256];
} n64_inject_t;
n64_inject_t g_n64_inject __attribute__((aligned(16)));

static void inject_frame(void)
{
    volatile n64_inject_t *in = (volatile n64_inject_t *)UncachedAddr(&g_n64_inject);
    in->magic = 0x54363449;
    in->frame++;
    while (in->tail != in->head) {
        dos_keyboard_scancode(in->codes[in->tail & 255]);
        in->tail++;
    }
    mouse_dx += in->mouse_dx;
    mouse_dy += in->mouse_dy;
    in->mouse_dx = in->mouse_dy = 0;
}

/* Controller look: stick to mouse counts per frame, with a dead zone. */
#define STICK_DEADZONE 8
#define STICK_SCALE    6            /* counts per frame at full tilt / 80 */

unsigned long dos_keyboard_irq_save(void)
{
    disable_interrupts();
    return 0;
}

void dos_keyboard_irq_restore(unsigned long state)
{
    (void)state;
    enable_interrupts();
}

void n64_input_init(void)
{
    joypad_init();
    dos_keyboard_reset();
}

static int stick(int v)
{
    if (v > -STICK_DEADZONE && v < STICK_DEADZONE)
        return 0;
    return v * STICK_SCALE / 80;
}

static uint16_t raw_buttons(const joypad_inputs_t *in)
{
    return (uint16_t)((in->btn.a << 15) | (in->btn.b << 14) | (in->btn.z << 13) | (in->btn.start << 12) |
                      (in->btn.c_up << 11) | (in->btn.c_down << 10) | (in->btn.c_left << 9) |
                      (in->btn.c_right << 8) | (in->btn.l << 5) | (in->btn.r << 4) |
                      (in->btn.d_up << 3) | (in->btn.d_down << 2) | (in->btn.d_left << 1) |
                      in->btn.d_right);
}

void n64_input_frame(void)
{
    joypad_poll();
    uint16_t now = 0;
    JOYPAD_PORT_FOREACH(port) {
        joypad_style_t style = joypad_get_style(port);
        if (style == JOYPAD_STYLE_NONE)
            continue;
        joypad_inputs_t in = joypad_get_inputs(port);
        if (style == JOYPAD_STYLE_MOUSE) {
            /* Towns convention: right and down positive. */
            mouse_dx += in.stick_x;
            mouse_dy -= in.stick_y;
            mouse_buttons = (in.btn.a ? MOUSE_L : 0) | (in.btn.b ? MOUSE_R : 0);
        } else {
            now |= raw_buttons(&in);
            mouse_dx += stick(in.stick_x);
            mouse_dy -= stick(in.stick_y);
        }
    }
    for (unsigned i = 0; i < NBUTTONS; i++) {
        uint16_t m = buttons[i].mask;
        if ((now & m) && !(held & m))
            dos_keyboard_scancode(buttons[i].code);
        else if (!(now & m) && (held & m))
            dos_keyboard_scancode(buttons[i].code | 0x80);
    }
    held = now;
    inject_frame();
}

int mouse_read(int *dx, int *dy)
{
    *dx = mouse_dx;
    *dy = mouse_dy;
    mouse_dx = mouse_dy = 0;
    return mouse_buttons | ((volatile n64_inject_t *)UncachedAddr(&g_n64_inject))->mouse_buttons;
}

u32 pad_read(void)
{
    return 0;   /* Controllers come in as keys and mouse motion above. */
}
