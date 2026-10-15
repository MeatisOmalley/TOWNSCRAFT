/* N64 input for DOScraft's unchanged Towns input API (Q5).
 *
 * Official interface: N64 Mouse + Randnet keyboard (keyboard_frame below;
 * in ares it needs the pinned fork, tools/run_ares.ps1 -Randnet).
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
#include <string.h>
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

static void keyboard_frame(void);
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
    uint32_t frame_us;          /* last present-to-present time */
    uint32_t frame_us_sum;      /* running sum, for averages over a window */
    uint8_t codes[256];
} n64_inject_t;
n64_inject_t g_n64_inject __attribute__((aligned(16)));

static void inject_frame(void)
{
    volatile n64_inject_t *in = (volatile n64_inject_t *)UncachedAddr(&g_n64_inject);
    static uint32_t last;
    uint32_t now = get_ticks();
    if (last) {
        in->frame_us = TICKS_TO_US(now - last);
        in->frame_us_sum += in->frame_us;
    }
    last = now;
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
    keyboard_frame();
    inject_frame();
}

/* ---- Randnet keyboard (research/randnet-keyboard-2026-10-15.md) ----
 * Identifier 0x0002; command 0x13 + LED byte returns three held keys (JIS
 * matrix positions, 0 = empty) and a status byte (bit 0 Home, bit 4 more
 * than three keys: slots empty). Keys map by position to the AT set-1
 * scancodes DOScraft's keyboard.c decodes; 0xE0xx codes are extended. */
static const struct { uint16_t matrix, at; } randnet_keys[] = {
    {0x0A08,0x01},{0x0B01,0x3B},{0x0A01,0x3C},{0x0B08,0x3D},{0x0A07,0x3E},{0x0B07,0x3F},
    {0x0A02,0x40},{0x0B02,0x41},{0x0A03,0x42},{0x0B03,0x43},{0x0A04,0x44},{0x0203,0x57},
    {0x0B06,0x58},
    {0x0C05,0x02},{0x0505,0x03},{0x0605,0x04},{0x0705,0x05},{0x0805,0x06},{0x0905,0x07},
    {0x0906,0x08},{0x0806,0x09},{0x0706,0x0A},{0x0606,0x0B},{0x0506,0x0C},{0x0C06,0x0D},
    {0x0D06,0x0E},
    {0x0D01,0x0F},{0x0C01,0x10},{0x0501,0x11},{0x0601,0x12},{0x0701,0x13},{0x0801,0x14},
    {0x0901,0x15},{0x0904,0x16},{0x0804,0x17},{0x0704,0x18},{0x0604,0x19},{0x0504,0x1A},
    {0x0C04,0x1B},{0x0406,0x2B},
    {0x0F05,0x3A},{0x0D07,0x1E},{0x0C07,0x1F},{0x0507,0x20},{0x0607,0x21},{0x0707,0x22},
    {0x0807,0x23},{0x0907,0x24},{0x0903,0x25},{0x0803,0x26},{0x0703,0x27},{0x0603,0x28},
    {0x0D04,0x1C},
    {0x0E01,0x2A},{0x0D08,0x2C},{0x0C08,0x2D},{0x0508,0x2E},{0x0608,0x2F},{0x0708,0x30},
    {0x0808,0x31},{0x0908,0x32},{0x0902,0x33},{0x0802,0x34},{0x0702,0x35},{0x0E06,0x36},
    {0x0204,0xE048},
    {0x1107,0x1D},{0x1008,0x38},{0x0602,0x39},{0x0206,0xE04F},{0x0205,0xE04B},
    {0x0305,0xE050},{0x0405,0xE04D},
};
#define RANDNET_ID 0x0002
#define HOME_AT 0xE047

static int keyboard_port = -1;
static uint16_t kb_held[4];          /* three slots + Home, as AT codes */

static uint8_t joybus_command(int port, const uint8_t *send, int send_len, uint8_t *recv, int recv_len)
{
    uint8_t input[JOYBUS_BLOCK_SIZE] = {0}, output[JOYBUS_BLOCK_SIZE];
    int i = port;                      /* leading 0x00 bytes skip channels */
    input[i++] = send_len;
    input[i++] = recv_len;
    memcpy(&input[i], send, send_len);
    i += send_len;
    int reply = i;
    memset(&input[i], 0xff, recv_len);
    i += recv_len;
    input[i] = 0xfe;
    input[JOYBUS_BLOCK_SIZE - 1] = 0x01;
    joybus_exec(input, output);
    memcpy(recv, &output[reply], recv_len);
    return output[port + 1];
}

static uint16_t randnet_to_at(uint16_t matrix)
{
    for (unsigned i = 0; i < sizeof(randnet_keys) / sizeof(randnet_keys[0]); i++)
        if (randnet_keys[i].matrix == matrix)
            return randnet_keys[i].at;
    return 0;
}

static void at_event(uint16_t code, int down)
{
    if (code >> 8)
        dos_keyboard_scancode(code >> 8);
    dos_keyboard_scancode((code & 0xFF) | (down ? 0 : 0x80));
}

static int in_set(const uint16_t *set, uint16_t code)
{
    for (int i = 0; i < 4; i++)
        if (set[i] == code) return 1;
    return 0;
}

static void keyboard_frame(void)
{
    static unsigned probe;
    if (keyboard_port < 0) {
        /* Look for a keyboard about once a second on ports libdragon doesn't know. */
        if (probe++ % 60)
            return;
        for (int port = 0; port < 4; port++) {
            if (joypad_get_style(port) != JOYPAD_STYLE_NONE)
                continue;
            uint8_t cmd = 0x00, info[3];
            if (!(joybus_command(port, &cmd, 1, info, 3) & 0x80) && (info[0] << 8 | info[1]) == RANDNET_ID) {
                keyboard_port = port;
                break;
            }
        }
        if (keyboard_port < 0)
            return;
    }
    uint8_t cmd[2] = { 0x13, 0x04 }, reply[7];       /* LED byte: Power on */
    if (joybus_command(keyboard_port, cmd, 2, reply, 7) & 0x80) {
        /* Unplugged: release everything and look again later. */
        for (int i = 0; i < 4; i++)
            if (kb_held[i]) at_event(kb_held[i], 0);
        memset(kb_held, 0, sizeof(kb_held));
        keyboard_port = -1;
        return;
    }
    if (reply[6] & 0x10)
        return;                     /* more than three keys: keep the last state */
    uint16_t now[4] = {0};
    for (int i = 0; i < 3; i++)
        now[i] = randnet_to_at(reply[i * 2] << 8 | reply[i * 2 + 1]);
    now[3] = (reply[6] & 0x01) ? HOME_AT : 0;
    for (int i = 0; i < 4; i++)
        if (kb_held[i] && !in_set(now, kb_held[i])) at_event(kb_held[i], 0);
    for (int i = 0; i < 4; i++)
        if (now[i] && !in_set(kb_held, now[i])) at_event(now[i], 1);
    memcpy(kb_held, now, sizeof(now));
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
