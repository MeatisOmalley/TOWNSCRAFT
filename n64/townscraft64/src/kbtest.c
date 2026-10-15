/* Townscraft64 Randnet keyboard test ROM.
 *
 * Talks to the four controller ports with raw Joybus commands only (no
 * joypad subsystem): 0x00 info every frame, and on a port that answers with
 * identifier 0x0002 the Randnet "read keypress" command 0x13 with an LED byte.
 * Shows the raw 7-byte reply, the decoded keys, the status byte and the last
 * key presses. Caps Lock and Num Lock toggle their LEDs; the Power LED is on
 * while the keyboard is polled. At start it also sends 0x01 (controller read)
 * to each port and shows the reply flags, since the keyboard should not answer
 * it. Protocol notes: research/randnet-keyboard-2026-10-15.md. */
#include <libdragon.h>
#include <string.h>

#define FONT_DEBUG 1

#define CMD_INFO      0x00
#define CMD_READ_PAD  0x01
#define CMD_KEYPRESS  0x13
#define ID_RANDNET    0x0002

#define LED_NUM   0x01
#define LED_CAPS  0x02
#define LED_POWER 0x04

#define STATUS_HOME     0x01
#define STATUS_TOO_MANY 0x10

static const struct { uint16_t code; const char *name; } key_names[] = {
    {0x0a08, "Esc"}, {0x0b01, "F1"}, {0x0a01, "F2"}, {0x0b08, "F3"}, {0x0a07, "F4"},
    {0x0b07, "F5"}, {0x0a02, "F6"}, {0x0b02, "F7"}, {0x0a03, "F8"}, {0x0b03, "F9"},
    {0x0a04, "F10"}, {0x0203, "F11"}, {0x0b06, "F12"}, {0x0a05, "NumLock"},
    {0x0b05, "J-NumLED"}, {0x0208, "J-CapsLED"}, {0x0207, "J-PowerLED"},
    {0x0d05, "Hankaku"}, {0x0c05, "1"}, {0x0505, "2"}, {0x0605, "3"}, {0x0705, "4"},
    {0x0805, "5"}, {0x0905, "6"}, {0x0906, "7"}, {0x0806, "8"}, {0x0706, "9"},
    {0x0606, "0"}, {0x0506, "-"}, {0x0c06, "^"}, {0x0d06, "BkSp"},
    {0x0d01, "Tab"}, {0x0c01, "Q"}, {0x0501, "W"}, {0x0601, "E"}, {0x0701, "R"},
    {0x0801, "T"}, {0x0901, "Y"}, {0x0904, "U"}, {0x0804, "I"}, {0x0704, "O"},
    {0x0604, "P"}, {0x0504, "'"}, {0x0c04, "{"}, {0x0406, "}"},
    {0x0f05, "CapsLock"}, {0x0d07, "A"}, {0x0c07, "S"}, {0x0507, "D"}, {0x0607, "F"},
    {0x0707, "G"}, {0x0807, "H"}, {0x0907, "J"}, {0x0903, "K"}, {0x0803, "L"},
    {0x0703, "+"}, {0x0603, "*"}, {0x0d04, "Enter"},
    {0x0e01, "LShift"}, {0x0d08, "Z"}, {0x0c08, "X"}, {0x0508, "C"}, {0x0608, "V"},
    {0x0708, "B"}, {0x0808, "N"}, {0x0908, "M"}, {0x0902, "<"}, {0x0802, ">"},
    {0x0702, "?"}, {0x1004, "LongDash"}, {0x0204, "Up"}, {0x0e06, "RShift"},
    {0x1107, "Ctrl"}, {0x0f07, "Opt"}, {0x1105, "|"}, {0x1008, "Alt"}, {0x1002, "Eisu"},
    {0x0602, "Space"}, {0x0e02, "Kana"}, {0x1006, "JpChar"}, {0x0206, "End"},
    {0x0205, "Left"}, {0x0305, "Down"}, {0x0405, "Right"},
};

static const char *key_name(uint16_t code)
{
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++)
        if (key_names[i].code == code) return key_names[i].name;
    return "?";
}

/* One command on one port. Returns the reply-length byte as the PIF left it:
 * bit 7 set = no device answered, bit 6 set = reply longer than requested. */
static uint8_t joybus_command(int port, const uint8_t *send, int send_len,
                              uint8_t *recv, int recv_len)
{
    uint8_t input[JOYBUS_BLOCK_SIZE] = {0};
    uint8_t output[JOYBUS_BLOCK_SIZE];
    int i = port;  /* a 0x00 byte skips a channel */
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

typedef struct {
    uint16_t id;
    uint8_t info_flags, info3;
    uint8_t pad_flags;         /* reply flags to 0x01 at start */
    uint8_t leds;
    uint8_t reply[7];
    uint8_t reply_flags;
    uint16_t held[3];          /* previous frame's key slots */
    uint8_t home;
} port_state_t;

static port_state_t ports[4];
static char history[8][24];
static int history_count;
static char line_buf[24];

static void remember(const char *text)
{
    memmove(history[1], history[0], sizeof(history) - sizeof(history[0]));
    strncpy(history[0], text, sizeof(history[0]) - 1);
    history[0][sizeof(history[0]) - 1] = 0;
    if (history_count < 8) history_count++;
}

static int was_held(const port_state_t *p, uint16_t code)
{
    for (int i = 0; i < 3; i++) if (p->held[i] == code) return 1;
    return 0;
}

static void poll_port(int port)
{
    port_state_t *p = &ports[port];
    uint8_t cmd[2] = {CMD_INFO, 0};
    uint8_t info[3];
    p->info_flags = joybus_command(port, cmd, 1, info, 3);
    p->id = (p->info_flags & 0x80) ? 0xffff : (info[0] << 8 | info[1]);
    p->info3 = info[2];
    if (p->id != ID_RANDNET) return;

    cmd[0] = CMD_KEYPRESS;
    cmd[1] = p->leds | LED_POWER;
    p->reply_flags = joybus_command(port, cmd, 2, p->reply, 7);
    if (p->reply_flags & 0x80) return;

    uint16_t now[3];
    for (int i = 0; i < 3; i++) now[i] = p->reply[i * 2] << 8 | p->reply[i * 2 + 1];
    for (int i = 0; i < 3; i++) {
        if (!now[i] || was_held(p, now[i])) continue;
        snprintf(line_buf, sizeof(line_buf), "P%d %04x %s", port + 1, now[i], key_name(now[i]));
        remember(line_buf);
        if (now[i] == 0x0f05) p->leds ^= LED_CAPS;
        if (now[i] == 0x0a05) p->leds ^= LED_NUM;
    }
    /* Home has no slot: it is bit 0 of the status byte */
    if ((p->reply[6] & STATUS_HOME) && !p->home) {
        snprintf(line_buf, sizeof(line_buf), "P%d Home", port + 1);
        remember(line_buf);
    }
    p->home = p->reply[6] & STATUS_HOME;
    memcpy(p->held, now, sizeof(now));
}

int main(void)
{
    debug_init_isviewer();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_DISABLED);
    rdpq_init();
    rdpq_text_register_font(FONT_DEBUG, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

    for (int port = 0; port < 4; port++) {
        uint8_t cmd = CMD_READ_PAD, pad[4];
        ports[port].pad_flags = joybus_command(port, &cmd, 1, pad, 4);
    }

    for (;;) {
        for (int port = 0; port < 4; port++) poll_port(port);

        surface_t *fb = display_get();
        rdpq_attach_clear(fb, NULL);
        int y = 14;
        rdpq_text_printf(NULL, FONT_DEBUG, 8, y, "Townscraft64 Randnet keyboard test");
        y += 14;
        for (int port = 0; port < 4; port++, y += 10) {
            port_state_t *p = &ports[port];
            if (p->id == 0xffff)
                rdpq_text_printf(NULL, FONT_DEBUG, 8, y, "P%d none        01:%02x", port + 1, p->pad_flags);
            else
                rdpq_text_printf(NULL, FONT_DEBUG, 8, y, "P%d id %04x %02x  01:%02x", port + 1,
                                 p->id, p->info3, p->pad_flags);
        }
        y += 6;
        for (int port = 0; port < 4; port++) {
            port_state_t *p = &ports[port];
            if (p->id != ID_RANDNET) continue;
            const uint8_t *r = p->reply;
            rdpq_text_printf(NULL, FONT_DEBUG, 8, y, "P%d 13: %02x%02x %02x%02x %02x%02x %02x f%02x",
                             port + 1, r[0], r[1], r[2], r[3], r[4], r[5], r[6], p->reply_flags);
            y += 10;
            rdpq_text_printf(NULL, FONT_DEBUG, 8, y, " keys %s %s %s%s%s",
                             p->held[0] ? key_name(p->held[0]) : "-",
                             p->held[1] ? key_name(p->held[1]) : "-",
                             p->held[2] ? key_name(p->held[2]) : "-",
                             (r[6] & STATUS_HOME) ? " HOME" : "",
                             (r[6] & STATUS_TOO_MANY) ? " TOO-MANY" : "");
            y += 10;
            rdpq_text_printf(NULL, FONT_DEBUG, 8, y, " LEDs Power %d Caps %d Num %d",
                             1, !!(p->leds & LED_CAPS), !!(p->leds & LED_NUM));
            y += 12;
        }
        rdpq_text_printf(NULL, FONT_DEBUG, 8, y, "Last presses:");
        y += 10;
        for (int i = 0; i < history_count; i++, y += 10)
            rdpq_text_printf(NULL, FONT_DEBUG, 16, y, "%s", history[i]);
        rdpq_detach_show();
    }
}
