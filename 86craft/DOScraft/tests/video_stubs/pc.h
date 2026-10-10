#ifndef TEST_PC_H
#define TEST_PC_H
unsigned char inportb(unsigned int port);
void outportb(unsigned int port,unsigned int value);
void outportw(unsigned int port,unsigned int value);
#endif
