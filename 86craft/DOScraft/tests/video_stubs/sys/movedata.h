#ifndef TEST_MOVEDATA_H
#define TEST_MOVEDATA_H
void dosmemput(const void *source,unsigned int bytes,unsigned int address);
void dosmemget(unsigned int address,unsigned int bytes,void *dest);
#endif
