/* Platform transport only: no camera scaling, cursor drawing or new controls.
 * Launch contract: pinned CuteMouse 1.9.1 /S14 /R11 /W /Y (linear counters).
 * INT 33 implementations can accelerate even function 0B; do not substitute
 * another driver without the serial-packet parity gate. */
#include "sys.h"
#include "input.h"
#include <dpmi.h>
#include <sys/movedata.h>

static int mouse_present;

static int mouse_call(unsigned int function, __dpmi_regs *regs)
{
    *regs = (__dpmi_regs){0};
    regs->x.ax = function;
    return __dpmi_int(0x33, regs) == 0;
}

void dos_input_shutdown(void) { mouse_present = 0; }
int dos_input_mouse_present(void) { return mouse_present; }

int dos_input_init(void)
{
    __dpmi_raddr vector;
    __dpmi_regs regs;
    unsigned char opcode;
    unsigned long address;
    /* Never invoke a null/IRET stub as if it were a driver. */
    dos_input_shutdown();
    if (__dpmi_get_real_mode_interrupt_vector(0x33, &vector)) return 0;
    address = (unsigned long)vector.segment * 16u + vector.offset16;
    if (!address) return 0;
    dosmemget(address, 1, &opcode);
    if (opcode == 0xCF) return 0;
    if (!mouse_call(0, &regs) || regs.x.ax != 0xFFFF) return 0;
    /* Discard pre-start movement. Do not show the software cursor in Mode X. */
    if (!mouse_call(0x0B, &regs)) return 0;
    mouse_present = 1;
    return 1;
}

static int signed_mickeys(unsigned int value)
{
    value &= 0xFFFFu;
    return value & 0x8000u ? (int)value - 65536 : (int)value;
}

int mouse_read(int *dx, int *dy)
{
    __dpmi_regs regs;
    int buttons;
    *dx = *dy = 0;
    if (!mouse_present) return 0;
    if (!mouse_call(3, &regs)) { dos_input_shutdown(); return 0; }
    buttons = regs.x.bx & (MOUSE_L | MOUSE_R);
    if (!mouse_call(0x0B, &regs)) { dos_input_shutdown(); return 0; }
    *dx = signed_mickeys(regs.x.cx);
    *dy = signed_mickeys(regs.x.dx);
    return buttons;
}

/* No gamepad is configured on the primary DOS target. Keyboard equivalents
 * remain in the unchanged gameplay code. Do not synthesize duplicate presses. */
u32 pad_read(void) { return 0; }
