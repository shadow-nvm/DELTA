#include "user.h"

int user_main(void)
{
    char buf[128];
    CapInfo ci;

    printf("[worker] pid=%d waiting on inherited endpoint cap %d\n",
           sys_getpid(), HANDLE_CHILD_ENDPOINT);
    if (sys_cap_info(HANDLE_CHILD_ENDPOINT, &ci) == 0) {
        printf("[worker] cap type=%u rights=0x%x object=%llu\n",
               ci.type, ci.rights, ci.id);
    }

    long n = sys_ipc_recv(HANDLE_CHILD_ENDPOINT, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("[worker] receive failed (%ld)\n", n);
        return 1;
    }
    buf[n] = 0;
    printf("[worker] received: %s\n", buf);
    return 42;
}
