#undef NDEBUG
#include <assert.h>
#include "bench_clock.h"
int main(void)
{
    unsigned int old,delta,epoch,skew;
    /* Read intervals from sub-microsecond to 20s, counter/IRQ wrap and
     * the worst +/- one IRQ epoch uncertainty around a latch. */
    for(old=0;old<65536;old+=7919)
        for(delta=1;delta<24000000;delta=delta*2+1)
            for(skew=0;skew<3;++skew) {
                epoch=delta/DOS_PIT_DIVISOR;
                if(skew==0 && epoch) --epoch;
                if(skew==2) ++epoch;
                assert(dos_bench_clock_delta(0xfffffff0u+epoch,0xfffffff0u,
                    (old-delta)&65535u,old)==delta);
            }
    return 0;
}
