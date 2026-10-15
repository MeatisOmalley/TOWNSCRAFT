/* Townscraft64 N1 bring-up: a spinning shaded triangle drawn by the RDP,
 * plus a per-port report of what is plugged into each controller port.
 * The port report is how N1 checks Mouse / Randnet keyboard support. */
#include <libdragon.h>
#include <math.h>

#define FONT_DEBUG 1

static const char *style_name(joypad_style_t style)
{
    switch (style) {
    case JOYPAD_STYLE_N64:   return "N64 pad";
    case JOYPAD_STYLE_GCN:   return "GC pad";
    case JOYPAD_STYLE_MOUSE: return "Mouse";
    default:                 return "none";
    }
}

int main(void)
{
    debug_init_isviewer();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_DISABLED);
    rdpq_init();
    joypad_init();
    rdpq_text_register_font(FONT_DEBUG, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

    float angle = 0.0f;
    uint32_t last = get_ticks();
    float frame_ms = 0.0f;

    for (;;) {
        joypad_poll();

        surface_t *fb = display_get();
        rdpq_attach_clear(fb, NULL);

        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        float v[3][6];
        for (int i = 0; i < 3; i++) {
            float a = angle + i * (2.0f * (float)M_PI / 3.0f);
            v[i][0] = 160.0f + cosf(a) * 90.0f;
            v[i][1] = 120.0f + sinf(a) * 90.0f;
            v[i][2] = i == 0; v[i][3] = i == 1; v[i][4] = i == 2; v[i][5] = 1.0f;
        }
        rdpq_triangle(&TRIFMT_SHADE, v[0], v[1], v[2]);

        rdpq_text_printf(NULL, FONT_DEBUG, 8, 14, "Townscraft64 N1  %.2f ms  RAM %d MB",
                         frame_ms, get_memory_size() >> 20);
        JOYPAD_PORT_FOREACH(port) {
            joypad_inputs_t in = joypad_get_inputs(port);
            rdpq_text_printf(NULL, FONT_DEBUG, 8, 196 + port * 10,
                             "P%d %-7s id %04x  x%+4d y%+4d",
                             port + 1, style_name(joypad_get_style(port)),
                             joybus_get_identifier(port, NULL), in.stick_x, in.stick_y);
        }

        rdpq_detach_show();

        uint32_t now = get_ticks();
        frame_ms = TICKS_TO_US(now - last) / 1000.0f;
        last = now;
        angle += 0.03f;
    }
}
