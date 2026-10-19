#ifndef DELTA_SYSCALL_H
#define DELTA_SYSCALL_H

#include <delta/interrupts.h>
#include <delta/types.h>

/* DELTA syscall numbers.  These are shared with userspace. */
enum {
    SYS_NONE = 0,
    SYS_EXIT = 1,
    SYS_WRITE = 2,
    SYS_READ = 3,
    SYS_YIELD = 4,
    SYS_GETPID = 5,
    SYS_SLEEP = 6,
    SYS_TIME = 7,
    SYS_IPC_CREATE = 8,
    SYS_IPC_SEND = 9,
    SYS_IPC_RECV = 10,
    SYS_SPAWN = 11,
    SYS_WAITPID = 12,
    SYS_CLOSE = 13,
    SYS_DUP = 14,
    SYS_DEBUG = 15,
    SYS_IDLE = 16,
    SYS_INFO = 17,
    SYS_EVENTLOG = 18,
    SYS_POWEROFF = 19,
    SYS_BOOTINFO = 20,
    SYS_GETPPID = 21,
    SYS_CAP_COUNT = 22,
    SYS_CAP_INFO = 23,
    SYS_SET_WEIGHT = 24,
    SYS_SBRK = 25,
    SYS_MAX
};

#define DELTA_EPOCH 1

typedef struct {
    u64 magic;
    u32 version;
    u32 flags;
    u64 ticks;
    u64 uptime_ms;
    u64 total_frames;
    u64 free_frames;
    u64 process_count;
    u64 running_pid;
    u64 console_writes;
    u64 context_switches;
} DeltaInfo;

typedef struct {
    u32 type;
    u32 rights;
    u64 id;
} CapInfo;

#define DELTA_INFO_MAGIC 0x4F464E4941544C44ULL  /* "DLTAINFO" */

u64 syscall_dispatch(TrapFrame *frame);

#endif
