#ifndef DELTA_PROCESS_H
#define DELTA_PROCESS_H

#include <delta/interrupts.h>
#include <delta/ipc.h>
#include <delta/object.h>
#include <delta/types.h>

#define MAX_PROCESSES 16
#define KSTACK_SIZE   16384
#define PROC_NAME_MAX 16

typedef enum {
    PROC_UNUSED = 0,
    PROC_RUNNABLE,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE,
} ProcState;

typedef enum {
    WAIT_NONE = 0,
    WAIT_IPC,
    WAIT_CONSOLE,
    WAIT_SLEEP,
    WAIT_CHILD,
} WaitKind;

typedef struct Process {
    u32 pid;
    u32 state;
    char name[PROC_NAME_MAX];

    u64 pml4;
    u8 *kstack;
    u64 kstack_top;
    u64 saved_rsp;

    u32 priority;
    u32 slice;
    u32 base_slice;
    u64 cpu_ticks;

    u32 wait_kind;
    Endpoint *wait_endpoint;
    u64 wait_arg;
    u64 wait_buf;
    u64 wait_len;
    u64 wake_tick;
    u64 wait_result;

    int parent_pid;
    u64 exit_code;
    Cap caps[MAX_CAPS];
} Process;

void process_init(void);
void process_set_boot_flags(u32 flags);
u32  process_boot_flags(void);
Process *process_current(void);
Process *process_find(int pid);
Process *process_find_child(Process *parent, int pid);
int  process_count(void);
u64  sched_context_switches(void);

Process *process_spawn(Process *parent, const char *name, u64 cap_handle);
void process_reap_children(Process *parent);

bool process_user_range(Process *p, u64 ptr, u64 len, bool write);
bool process_copy_from(Process *p, void *dst, u64 src, u64 len);
bool process_copy_to(Process *p, u64 dst, const void *src, u64 len);

void process_wake(Process *p);
void process_wake_sleepers(void);
void process_console_wake(void);

void sched_start(void) NORETURN;
u64  sched_yield_current(TrapFrame *frame);
u64  sched_block_current(TrapFrame *frame, WaitKind kind);
u64  sched_tick_current(TrapFrame *frame);
u64  process_exit_current(TrapFrame *frame, int code);
bool sched_other_runnable(void);

/* cpu.asm */
void delta_enter_user(TrapFrame *frame) NORETURN;

#endif
