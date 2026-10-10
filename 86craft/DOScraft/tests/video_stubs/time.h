#ifndef TEST_UCLOCK_H
#define TEST_UCLOCK_H
typedef long long uclock_t;
#define UCLOCKS_PER_SEC 1193182
uclock_t uclock(void);
#endif
