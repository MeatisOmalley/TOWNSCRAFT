#include "f2b_texel.h"

int dos_f2b_texel_init(struct DosF2BTexel *state, const uint8_t *tile,
                       int32_t u, int32_t v, int32_t du, int32_t dv, int wrap)
{
    uintptr_t address;
    uint32_t ui, vi;
    if (!state || !tile || (wrap != 0 && wrap != 1)) return -1;
    ui = ((uint32_t)u >> 16) & 15u;
    vi = ((uint32_t)v >> 16) & 15u;
    address = (uintptr_t)tile;
    if (!wrap) address += (vi << 8) + ui;
    state->base = wrap ? address : address & ~(uintptr_t)65535u;
    state->u_index = wrap ? (uint8_t)ui : (uint8_t)address;
    state->v_index = wrap ? (uint8_t)vi : (uint8_t)(address >> 8);
    state->u_fraction = (uint16_t)u;
    state->v_fraction = (uint16_t)v;
    state->du_fraction = (uint16_t)du;
    state->dv_fraction = (uint16_t)dv;
    /* Low byte of the signed integer step, including negative gradients. */
    state->u_step = (uint8_t)((uint32_t)du >> 16);
    state->v_step = (uint8_t)((uint32_t)dv >> 16);
    state->wrap = (uint8_t)wrap;
    return 1;
}

int dos_f2b_texel_jump(struct DosF2BTexel *state, int32_t n)
{
    uint32_t total_u, total_v, mask;
    if (!state || n < 0 || n > DOS_F2B_TEXEL_MAX_JUMP || state->wrap > 1 ||
        (state->wrap && (state->u_index > 15 || state->v_index > 15)) ||
        (!state->wrap && (state->base & (uintptr_t)65535u))) return -1;
    if (!n) return 1;
    /* At n <= 320, even 65535 + 65535*n fits comfortably in 32 bits. */
    total_u = state->u_fraction + (uint32_t)state->du_fraction * (uint32_t)n;
    total_v = state->v_fraction + (uint32_t)state->dv_fraction * (uint32_t)n;
    mask = state->wrap ? 15u : 255u;
    state->u_index = (uint8_t)((state->u_index +
        (uint32_t)state->u_step * (uint32_t)n + (total_u >> 16)) & mask);
    state->v_index = (uint8_t)((state->v_index +
        (uint32_t)state->v_step * (uint32_t)n + (total_v >> 16)) & mask);
    state->u_fraction = (uint16_t)total_u;
    state->v_fraction = (uint16_t)total_v;
    return 1;
}
