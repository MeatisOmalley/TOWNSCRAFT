/* Strict DOS commit for ordinary open() handles (not FSEXT virtual files).
 * Unlike DJGPP fsync, DOS errors 1 and 6 are not accepted as success. */
#ifndef DOSCRAFT_FILE_COMMIT_H
#define DOSCRAFT_FILE_COMMIT_H
#include <dpmi.h>
static int dos_file_commit(int fd)
{
    __dpmi_regs regs = {0};
    if (fd < 0 || fd > 0xFFFF) return -1;
    regs.x.ax = 0x6800;
    regs.x.bx = fd;
    if (__dpmi_int(0x21, &regs) || (regs.x.flags & 1)) return -1;
    return 0;
}
#endif
