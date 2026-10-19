#include <delta/process.h>
#include <delta/console.h>
#include <delta/elf.h>
#include <delta/event.h>
#include <delta/gdt.h>
#include <delta/input.h>
#include <delta/io.h>
#include <delta/paging.h>
#include <delta/pmm.h>
#include <delta/string.h>
#include <delta/timer.h>

extern const u8 init_elf_start[];
extern const u8 init_elf_end[];
extern const u8 worker_elf_start[];
extern const u8 worker_elf_end[];
extern const u8 spinner_elf_start[];
extern const u8 spinner_elf_end[];
extern const u8 idle_elf_start[];
extern const u8 idle_elf_end[];

typedef struct {
    const char *name;
    const u8 *image;
    const u8 *end;
} UserProgram;

static const UserProgram g_programs[] = {
    { "init",    init_elf_start,    init_elf_end },
    { "worker",  worker_elf_start,  worker_elf_end },
    { "spinner", spinner_elf_start, spinner_elf_end },
    { "idle",    idle_elf_start,    idle_elf_end },
};

static Process g_procs[MAX_PROCESSES];
static u8 g_kstacks[MAX_PROCESSES][KSTACK_SIZE] ALIGNED(16);
static Process *g_current;
static int g_last_index;
static u64 g_switches;
static u32 g_next_pid = 1;
static u32 g_boot_flags;

static usize program_size(const UserProgram *p)
{
    return (usize)(p->end - p->image);
}

static void process_grant_console(Process *p)
{
    Object *console = object_console();
    cap_grant(p, console, RIGHT_WRITE);  /* 1: stdout */
    cap_grant(p, console, RIGHT_WRITE);  /* 2: stderr */
    cap_grant(p, console, RIGHT_READ);   /* 3: stdin */
}

static Process *process_create(const char *name, const u8 *elf, usize elf_size,
                               u32 priority, u32 slice, bool console_caps)
{
    Process *p = 0;
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_procs[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return 0;
    }
    p = &g_procs[slot];
    memset(p, 0, sizeof(*p));
    p->pid = g_next_pid++;
    p->state = PROC_RUNNABLE;
    p->parent_pid = 0;
    p->priority = priority;
    p->base_slice = slice;
    p->slice = slice;
    strncpy(p->name, name, PROC_NAME_MAX - 1);

    p->pml4 = vmm_create_space();
    if (!p->pml4) {
        memset(p, 0, sizeof(*p));
        return 0;
    }

    u64 entry = 0;
    if (!elf_load_user(p->pml4, elf, elf_size, &entry)) {
        vmm_destroy_space(p->pml4);
        memset(p, 0, sizeof(*p));
        return 0;
    }

    for (u32 i = 0; i < USER_STACK_PAGES; i++) {
        u64 va = USER_STACK_TOP - (u64)(i + 1) * PAGE_SIZE;
        if (!vmm_map_user_alloc(p->pml4, va, PAGE_WRITE | PAGE_NX)) {
            vmm_destroy_space(p->pml4);
            memset(p, 0, sizeof(*p));
            return 0;
        }
    }

    p->kstack = &g_kstacks[slot][0];
    p->kstack_top = (u64)(uintptr_t)(p->kstack + KSTACK_SIZE);
    p->kstack_top &= ~15ULL;

    TrapFrame *frame = (TrapFrame *)(uintptr_t)(p->kstack_top - sizeof(TrapFrame));
    memset(frame, 0, sizeof(*frame));
    frame->rip = entry;
    frame->cs = USER_CS;
    frame->rflags = 0x202;      /* IF=1, reserved bit 1 set */
    frame->rsp = USER_STACK_TOP;
    frame->ss = USER_DS;
    frame->vector = 0;
    frame->error = 0;
    p->saved_rsp = (u64)(uintptr_t)frame;

    if (console_caps) {
        process_grant_console(p);
    }
    return p;
}

void process_init(void)
{
    memset(g_procs, 0, sizeof(g_procs));
    g_current = 0;
    g_last_index = -1;
    g_switches = 0;
    g_next_pid = 1;
    g_boot_flags = 0;

    objects_init();
    input_set_wake(process_console_wake);

    Process *idle = process_create("idle", idle_elf_start,
                                   (usize)(idle_elf_end - idle_elf_start),
                                   15, 1, false);
    if (!idle) {
        kpanic("could not create the idle process");
    }
    Process *init = process_create("init", init_elf_start,
                                   (usize)(init_elf_end - init_elf_start),
                                   5, 4, true);
    if (!init) {
        kpanic("could not create init");
    }
    kprintf("[proc] idle pid=%u, init pid=%u; %u slots\n",
            idle->pid, init->pid, MAX_PROCESSES);
    kprintf("[cap] init owns %d capabilities (stdout, stderr, stdin)\n",
            cap_count(init));
}

void process_set_boot_flags(u32 flags)
{
    g_boot_flags = flags;
}

u32 process_boot_flags(void)
{
    return g_boot_flags;
}

Process *process_current(void)
{
    return g_current;
}

Process *process_find(int pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_procs[i].state != PROC_UNUSED && g_procs[i].pid == (u32)pid) {
            return &g_procs[i];
        }
    }
    return 0;
}

Process *process_find_child(Process *parent, int pid)
{
    if (!parent) {
        return 0;
    }
    Process *found = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_procs[i];
        if (p->state == PROC_UNUSED || p->parent_pid != (int)parent->pid) {
            continue;
        }
        if (pid == -1 || p->pid == (u32)pid) {
            found = p;
            if (pid != -1) {
                break;
            }
        }
    }
    return found;
}

int process_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_procs[i].state != PROC_UNUSED) {
            n++;
        }
    }
    return n;
}

u64 sched_context_switches(void)
{
    return g_switches;
}

bool process_user_range(Process *p, u64 ptr, u64 len, bool write)
{
    if (!p) {
        return false;
    }
    return vmm_user_range_ok(p->pml4, ptr, len, write);
}

bool process_copy_from(Process *p, void *dst, u64 src, u64 len)
{
    if (!p || !process_user_range(p, src, len, false)) {
        return false;
    }
    u64 old = read_cr3();
    if (old != p->pml4) {
        vmm_switch(p->pml4);
    }
    memcpy(dst, (const void *)(uintptr_t)src, (usize)len);
    if (old != p->pml4) {
        vmm_switch(old);
    }
    return true;
}

bool process_copy_to(Process *p, u64 dst, const void *src, u64 len)
{
    if (!p || !process_user_range(p, dst, len, true)) {
        return false;
    }
    u64 old = read_cr3();
    if (old != p->pml4) {
        vmm_switch(p->pml4);
    }
    memcpy((void *)(uintptr_t)dst, src, (usize)len);
    if (old != p->pml4) {
        vmm_switch(old);
    }
    return true;
}

void process_wake(Process *p)
{
    if (!p) {
        return;
    }
    if (p->state == PROC_BLOCKED) {
        p->state = PROC_RUNNABLE;
        p->wait_kind = WAIT_NONE;
        p->wait_endpoint = 0;
    }
}

void process_wake_sleepers(void)
{
    u64 now = timer_ticks();
    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_procs[i];
        if (p->state == PROC_BLOCKED && p->wait_kind == WAIT_SLEEP &&
            now >= p->wake_tick) {
            TrapFrame *f = (TrapFrame *)(uintptr_t)p->saved_rsp;
            if (f) {
                f->rax = 0;
            }
            p->wait_result = 0;
            process_wake(p);
            p->wake_tick = 0;
        }
    }
}

void process_console_wake(void)
{
    if (input_count() == 0) {
        return;
    }
    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_procs[i];
        if (p->state != PROC_BLOCKED || p->wait_kind != WAIT_CONSOLE) {
            continue;
        }
        char tmp[64];
        u64 n = 0;
        while (n < p->wait_len && n < sizeof(tmp)) {
            int c = input_pop();
            if (c < 0) {
                break;
            }
            tmp[n++] = (char)c;
        }
        if (n == 0) {
            return;
        }
        if (!process_copy_to(p, p->wait_buf, tmp, n)) {
            return;
        }
        TrapFrame *f = (TrapFrame *)(uintptr_t)p->saved_rsp;
        if (f) {
            f->rax = n;
        }
        p->wait_endpoint = 0;
        process_wake(p);
        return;
    }
}

static Process *pick_next(void)
{
    Process *best = 0;
    int best_index = -1;
    for (int n = 0; n < MAX_PROCESSES; n++) {
        int i = (g_last_index + 1 + n) % MAX_PROCESSES;
        Process *p = &g_procs[i];
        if (p->state != PROC_RUNNABLE) {
            continue;
        }
        if (!best || p->priority < best->priority) {
            best = p;
            best_index = i;
        }
    }
    if (best) {
        g_last_index = best_index;
    }
    return best;
}

bool sched_other_runnable(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_procs[i];
        if (p != g_current && p->state == PROC_RUNNABLE) {
            return true;
        }
    }
    return false;
}

static u64 switch_to(Process *next)
{
    if (!next) {
        kpanic("scheduler has no runnable process");
    }
    next->state = PROC_RUNNING;
    g_current = next;
    vmm_switch(next->pml4);
    tss_set_rsp0(next->kstack_top);
    g_switches++;
    return next->saved_rsp;
}

void sched_start(void)
{
    Process *next = pick_next();
    if (!next) {
        kpanic("no initial process");
    }
    interrupts_disable();
    next->state = PROC_RUNNING;
    g_current = next;
    vmm_switch(next->pml4);
    tss_set_rsp0(next->kstack_top);
    kprintf("[sched] first user frame: %s pid=%u rip=%p\n",
            next->name, next->pid,
            (void *)((TrapFrame *)(uintptr_t)next->saved_rsp)->rip);
    delta_enter_user((TrapFrame *)(uintptr_t)next->saved_rsp);
}

u64 sched_yield_current(TrapFrame *frame)
{
    Process *prev = g_current;
    if (!prev) {
        return 0;
    }
    if (frame) {
        prev->saved_rsp = (u64)(uintptr_t)frame;
    }
    prev->state = PROC_RUNNABLE;
    prev->slice = prev->base_slice;
    Process *next = pick_next();
    if (!next || next == prev) {
        prev->state = PROC_RUNNING;
        return 0;
    }
    return switch_to(next);
}

u64 sched_block_current(TrapFrame *frame, WaitKind kind)
{
    Process *prev = g_current;
    if (!prev) {
        return 0;
    }
    if (frame) {
        prev->saved_rsp = (u64)(uintptr_t)frame;
    }
    prev->state = PROC_BLOCKED;
    prev->wait_kind = kind;
    Process *next = pick_next();
    if (!next) {
        kpanic("all processes blocked with no idle process");
    }
    return switch_to(next);
}

u64 sched_tick_current(TrapFrame *frame)
{
    Process *p = g_current;
    if (!p || p->state != PROC_RUNNING) {
        return 0;
    }
    p->cpu_ticks++;
    if (p->slice > 0) {
        p->slice--;
    }
    if (p->slice > 0) {
        return 0;
    }
    p->slice = p->base_slice;
    if (frame) {
        p->saved_rsp = (u64)(uintptr_t)frame;
    }
    p->state = PROC_RUNNABLE;
    Process *next = pick_next();
    if (!next || next == p) {
        p->state = PROC_RUNNING;
        return 0;
    }
    return switch_to(next);
}

u64 process_exit_current(TrapFrame *frame, int code)
{
    (void)frame;
    Process *prev = g_current;
    if (!prev) {
        for (;;) {
            cpu_halt();
        }
    }
    prev->exit_code = (u64)code;
    prev->state = PROC_ZOMBIE;
    event_log(EVT_PROC_EXIT, prev->pid, (u64)(u32)code, prev->parent_pid);

    Process *parent = process_find(prev->parent_pid);
    if (parent && parent->state == PROC_BLOCKED &&
        parent->wait_kind == WAIT_CHILD &&
        (parent->wait_arg == 0 || parent->wait_arg == prev->pid)) {
        int status = code;
        if (parent->wait_buf) {
            process_copy_to(parent, parent->wait_buf, &status, sizeof(status));
        }
        parent->wait_result = prev->pid;
        TrapFrame *pf = (TrapFrame *)(uintptr_t)parent->saved_rsp;
        if (pf) {
            pf->rax = prev->pid;
        }
        parent->wait_arg = 0;
        process_wake(parent);
    }

    cap_close_all(prev);
    Process *next = pick_next();
    if (!next) {
        kpanic("no process left after exit");
    }
    return switch_to(next);
}

void process_reap_children(Process *parent)
{
    if (!parent) {
        return;
    }
    for (int i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_procs[i];
        if (p->state == PROC_ZOMBIE && p->parent_pid == (int)parent->pid) {
            cap_close_all(p);
            if (p->pml4) {
                vmm_destroy_space(p->pml4);
            }
            memset(p, 0, sizeof(*p));
        }
    }
}

Process *process_spawn(Process *parent, const char *name, u64 cap_handle)
{
    if (!parent || !name) {
        return 0;
    }
    const UserProgram *prog = 0;
    for (u32 i = 0; i < ARRAY_LEN(g_programs); i++) {
        if (!strcmp(name, g_programs[i].name)) {
            prog = &g_programs[i];
            break;
        }
    }
    if (!prog || !strcmp(name, "init") || !strcmp(name, "idle")) {
        return 0;
    }

    Object *cap_obj = 0;
    u32 rights = 0;
    if (cap_handle) {
        if (!cap_rights(parent, cap_handle, &rights, &cap_obj)) {
            return 0;
        }
        if (!(rights & RIGHT_TRANSFER)) {
            return 0; /* authority cannot leave the process without TRANSFER */
        }
    }

    process_reap_children(parent);
    Process *child = process_create(name, prog->image, program_size(prog), 5, 4, true);
    if (!child) {
        return 0;
    }
    child->parent_pid = (int)parent->pid;
    if (cap_obj) {
        int h = cap_grant(child, cap_obj, rights & (RIGHT_SEND | RIGHT_RECV | RIGHT_TRANSFER));
        if (h < 0) {
            vmm_destroy_space(child->pml4);
            memset(child, 0, sizeof(*child));
            return 0;
        }
    }
    kprintf("[proc] spawned %s pid=%u parent=%u\n", child->name, child->pid, parent->pid);
    event_log(EVT_PROC_SPAWN, child->pid, parent->pid, child->pml4);
    return child;
}
