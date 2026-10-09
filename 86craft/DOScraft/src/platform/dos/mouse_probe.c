/* Diagnostic only. UART loopback -> IRQ4 -> real-mode CuteMouse -> INT 33.
 * Tests the driver transport, NOT Windows mouse capture or physical ingress.
 * No interrupt handler callbacks, drawing cursor, sensitivity or camera changes. */
#include "sys.h"
#include "input.h"
#include "system.h"
#include "video_backend.h"
#include <pc.h>
#include <stdio.h>

#define COM1 0x3F8
static int trace[32][6], trace_count;
static int wait_ticks(unsigned int amount)
{
    unsigned int start = g_ticks, guard = 10000000;
    while (g_ticks - start < amount && --guard) {}
    return guard != 0;
}
static int uart_idle(void)
{
    unsigned int start = g_ticks, guard = 10000000;
    while ((inportb(COM1 + 5) & 0x60) != 0x60 && --guard)
        if (g_ticks - start > 50) return 0;
    return guard != 0;
}
static int packet(int dx, int dy, int buttons)
{
    unsigned int x = (unsigned int)dx & 255u, y = (unsigned int)dy & 255u;
    unsigned char bytes[3];
    int i;
    bytes[0] = 0x40 | ((buttons & MOUSE_L) ? 0x20 : 0) |
        ((buttons & MOUSE_R) ? 0x10 : 0) | ((y >> 4) & 0x0C) | (x >> 6);
    bytes[1] = x & 0x3F;
    bytes[2] = y & 0x3F;
    for (i = 0; i < 3; ++i) {
        if (!uart_idle()) return 0;
        outportb(COM1, bytes[i]);
    }
    return uart_idle() && wait_ticks(3);
}
static int read_expected(int x, int y, int buttons)
{
    int dx, dy, got = mouse_read(&dx, &dy);
    if (trace_count < 32) {
        int *row = trace[trace_count++];
        row[0]=x; row[1]=y; row[2]=buttons; row[3]=dx; row[4]=dy; row[5]=got;
    }
    if (dx != x || dy != y || got != buttons) {
        printf("Expected (%d,%d,%d), got (%d,%d,%d)\n", x,y,buttons,dx,dy,got);
        return 0;
    }
    return dos_input_mouse_present();
}
int main(void)
{
    unsigned char mcr;
    int present, config, packets = 1, consumed = 1, lifecycle = 0, restored;
    int accumulation = 1;
    int i, ok, dx, dy;
    FILE *report;
    puts("DOScraft serial mouse driver diagnostic (not the game)");
    sys_init();
    present = dos_input_mouse_present();
    mcr = inportb(COM1 + 4);
    config = !(inportb(COM1 + 3) & 0x80) && (inportb(COM1 + 1) & 1) &&
        (mcr & 8) && !(inportb(0x21) & 0x10);
    video_init();
    if (present && config && uart_idle()) {
        outportb(COM1 + 4, mcr | 0x10); /* Preserve DTR/RTS/OUT2. */
        packets &= wait_ticks(3) && read_expected(0,0,0);
        for (i = 0; i < 4; ++i) {
            int x = i & 1 ? -127 : 127, y = i & 2 ? -128 : 127;
            packets &= packet(x,y,i) && read_expected(x,y,i);
            consumed &= read_expected(0,0,i);
        }
        packets &= packet(7,-5,MOUSE_L) && packet(-11,9,MOUSE_R) && read_expected(-4,4,MOUSE_R);
        packets &= packet(0,0,0) && read_expected(0,0,0);
        for (i = 0; i < 3; ++i) accumulation &= packet(127,127,MOUSE_L);
        accumulation &= read_expected(381,381,MOUSE_L);
        consumed &= read_expected(0,0,MOUSE_L);
        for (i = 0; i < 3; ++i) accumulation &= packet(-128,-128,MOUSE_R);
        accumulation &= read_expected(-384,-384,MOUSE_R);
        consumed &= read_expected(0,0,MOUSE_R);
        accumulation &= packet(0,0,0) && read_expected(0,0,0);
        packets &= packet(1,-1,MOUSE_L) && read_expected(1,-1,MOUSE_L);
        packets &= packet(0,0,0) && read_expected(0,0,0);
        consumed &= read_expected(0,0,0);
        outportb(COM1 + 4, mcr);
        dos_input_shutdown();
        dx = dy = 99;
        lifecycle = !dos_input_mouse_present() && !mouse_read(&dx,&dy) && !dx && !dy;
        lifecycle &= dos_input_init() && read_expected(0,0,0);
        /* Driver reset must retain linear mode, not just its presence bit. */
        outportb(COM1 + 4, mcr | 0x10);
        lifecycle &= packet(5,-7,MOUSE_L | MOUSE_R) && read_expected(5,-7,MOUSE_L | MOUSE_R);
        lifecycle &= packet(0,0,0) && read_expected(0,0,0);
        outportb(COM1 + 4, mcr);
    } else packets = consumed = accumulation = 0;
    restored = inportb(COM1 + 4) == mcr;
    dos_video_restore();
    dos_system_shutdown();
    lifecycle &= !dos_input_mouse_present();
    ok = present && config && packets && consumed && accumulation && lifecycle && restored;
    report = fopen("C:\\MOUSE.TXT", "wb");
    if (!report) return 1;
    fprintf(report,"DOScraft DOS mouse diagnostic\r\nMOUSE_DRIVER=%s\r\nCOM1_IRQ4=%s\r\n"
        "SERIAL_LOOPBACK_PACKETS=%s\r\nRELATIVE_CONSUMPTION=%s\r\nACCUMULATION_16BIT=%s\r\nLIFECYCLE=%s\r\n"
        "UART_RESTORE=%s\r\nHOST_MOUSE_CAPTURE=UNTESTED\r\nRESULT=%s\r\n",
        present?"PASS":"FAIL",config?"PASS":"FAIL",packets?"PASS":"FAIL",
        consumed?"PASS":"FAIL",accumulation?"PASS":"FAIL",lifecycle?"PASS":"FAIL",restored?"PASS":"FAIL",ok?"PASS":"FAIL");
    for (i=0;i<trace_count;++i)
        fprintf(report,"READ_%d=expected(%d,%d,%d) actual(%d,%d,%d)\r\n",i,
            trace[i][0],trace[i][1],trace[i][2],trace[i][3],trace[i][4],trace[i][5]);
    fclose(report);
    puts(ok ? "DOS serial mouse: PASS" : "DOS serial mouse: FAIL (see C:\\MOUSE.TXT)");
    return !ok;
}
