#include "user.h"

/* A deliberately CPU-bound program used by the self-test to exercise timer
 * preemption and address-space switching between two runnable processes. */
int user_main(void)
{
    u64 start = (u64)sys_time();
    volatile u64 acc = 0;

    while ((u64)sys_time() - start < 120) {
        for (volatile u64 i = 0; i < 25000; i++) {
            acc += i;
        }
    }
    printf("[spinner] pid=%d finished 120 ms of CPU work\n", sys_getpid());
    return 7;
}
