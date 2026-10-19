#ifndef DELTA_USER_H
#define DELTA_USER_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed long long   i64;
typedef unsigned long      usize;
typedef _Bool              bool;
#define true 1
#define false 0

#define NULL ((void *)0)

/* Syscall numbers: kept in lock-step with src/include/delta/syscall.h. */
enum {
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
};

#define BOOT_FLAG_SELFTEST 1

#define HANDLE_STDOUT 1
#define HANDLE_STDERR 2
#define HANDLE_STDIN  3
#define HANDLE_CHILD_ENDPOINT 4

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

#define DELTA_INFO_MAGIC 0x4F464E4941544C44ULL

typedef struct {
    u32 type;
    u32 rights;
    u64 id;
} CapInfo;

typedef struct {
    u64 seq;
    u64 tick;
    u32 type;
    u32 pad;
    u64 a;
    u64 b;
    u64 c;
} DeltaEvent;

enum {
    EVT_BOOT = 1,
    EVT_CAP_GRANT,
    EVT_CAP_CLOSE,
    EVT_PROC_SPAWN,
    EVT_PROC_EXIT,
    EVT_IPC_SEND,
    EVT_IPC_RECV,
    EVT_FAULT,
    EVT_SCHED,
    EVT_POWER,
};

/* Raw syscall ABI: number in rax, args in rdi/rsi/rdx/r10/r8/r9. */
static inline i64 sys_call3(u64 nr, u64 a, u64 b, u64 c)
{
    register u64 rax __asm__("rax") = nr;
    register u64 rdi __asm__("rdi") = a;
    register u64 rsi __asm__("rsi") = b;
    register u64 rdx __asm__("rdx") = c;
    __asm__ volatile("int $0x80"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx)
                     : "rcx", "r11", "memory");
    return (i64)rax;
}

static inline i64 sys_call4(u64 nr, u64 a, u64 b, u64 c, u64 d)
{
    register u64 rax __asm__("rax") = nr;
    register u64 rdi __asm__("rdi") = a;
    register u64 rsi __asm__("rsi") = b;
    register u64 rdx __asm__("rdx") = c;
    register u64 r10 __asm__("r10") = d;
    __asm__ volatile("int $0x80"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                     : "rcx", "r11", "memory");
    return (i64)rax;
}

static inline long sys_write(long fd, const void *buf, usize len)
{
    return (long)sys_call3(SYS_WRITE, (u64)fd, (u64)buf, (u64)len);
}
static inline long sys_read(long fd, void *buf, usize len)
{
    return (long)sys_call3(SYS_READ, (u64)fd, (u64)buf, (u64)len);
}
static inline void sys_exit(int code)
{
    sys_call3(SYS_EXIT, (u64)code, 0, 0);
    for (;;) {
    }
}
static inline void sys_yield(void)
{
    sys_call3(SYS_YIELD, 0, 0, 0);
}
static inline void sys_idle(void)
{
    sys_call3(SYS_IDLE, 0, 0, 0);
}
static inline int sys_getpid(void)
{
    return (int)sys_call3(SYS_GETPID, 0, 0, 0);
}
static inline int sys_getppid(void)
{
    return (int)sys_call3(SYS_GETPPID, 0, 0, 0);
}
static inline void sys_sleep(u64 ms)
{
    sys_call3(SYS_SLEEP, ms, 0, 0);
}
static inline long sys_ipc_create(void)
{
    return (long)sys_call3(SYS_IPC_CREATE, 0, 0, 0);
}
static inline long sys_ipc_send(long ep, const void *buf, usize len)
{
    return (long)sys_call3(SYS_IPC_SEND, (u64)ep, (u64)buf, (u64)len);
}
static inline long sys_ipc_recv(long ep, void *buf, usize len)
{
    return (long)sys_call3(SYS_IPC_RECV, (u64)ep, (u64)buf, (u64)len);
}
static inline long sys_spawn(const char *name, long cap)
{
    return (long)sys_call3(SYS_SPAWN, (u64)name, (u64)cap, 0);
}
static inline long sys_waitpid(long pid, int *status)
{
    return (long)sys_call3(SYS_WAITPID, (u64)pid, (u64)status, 0);
}
static inline long sys_info(DeltaInfo *info)
{
    return (long)sys_call3(SYS_INFO, (u64)info, 0, 0);
}
static inline long sys_bootinfo(void)
{
    return (long)sys_call3(SYS_BOOTINFO, 0, 0, 0);
}
static inline void sys_poweroff(void)
{
    sys_call3(SYS_POWEROFF, 0, 0, 0);
    for (;;) {
    }
}
static inline long sys_time(void)
{
    return (long)sys_call3(SYS_TIME, 0, 0, 0);
}
static inline long sys_cap_count(void)
{
    return (long)sys_call3(SYS_CAP_COUNT, 0, 0, 0);
}
static inline long sys_cap_info(long h, CapInfo *out)
{
    return (long)sys_call3(SYS_CAP_INFO, (u64)h, (u64)out, 0);
}
static inline long sys_eventlog(DeltaEvent *out, u32 max)
{
    return (long)sys_call3(SYS_EVENTLOG, (u64)out, (u64)max, 0);
}

void *memcpy(void *dst, const void *src, usize n);
void *memset(void *dst, int c, usize n);
usize strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, usize n);

void puts(const char *s);
void putc(char c);
void printf(const char *fmt, ...);
int read_line(char *buf, usize cap);

#endif
