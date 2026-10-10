#ifndef TEST_DPMI_H
#define TEST_DPMI_H
typedef struct { struct { unsigned short ax; } x; struct { unsigned char ah,al; } h; } __dpmi_regs;
int __dpmi_int(int number,__dpmi_regs *regs);
#endif
