#include <delta/syscall.h>
#include <delta/console.h>
#include <delta/errors.h>
#include <delta/event.h>
#include <delta/input.h>
#include <delta/io.h>
#include <delta/ipc.h>
#include <delta/object.h>
#include <delta/paging.h>
#include <delta/pmm.h>
#include <delta/process.h>
#include <delta/string.h>
#include <delta/timer.h>

static u64 g_console_writes;

static u64 sys_ret(TrapFrame *f, i64 value)
{
    f->rax = (u64)value;
    return 0;
}

static i64 sys_write(TrapFrame *f)
{
    Process *p = process_current();
    u64 h = f->rdi;
    u64 ptr = f->rsi;
    u64 len = f->rdx;
    Object *obj = cap_get(p, h, OBJ_CONSOLE, RIGHT_WRITE);
    if (!obj) {
        return -EBADF;
    }
    if (len == 0) {
        return 0;
    }
    if (len > 8192 || !process_user_range(p, ptr, len, false)) {
        return -EFAULT;
    }
    for (u64 i = 0; i < len; i++) {
        console_putc((char)((const u8 *)(uintptr_t)ptr)[i]);
        g_console_writes++;
    }
    return (i64)len;
}

static u64 sys_read_block(TrapFrame *f, Process *p)
{
    u64 ptr = f->rsi;
    u64 len = f->rdx;
    Object *obj = cap_get(p, f->rdi, OBJ_CONSOLE, RIGHT_READ);
    if (!obj) {
        return sys_ret(f, -EBADF);
    }
    if (len == 0) {
        return sys_ret(f, 0);
    }
    if (!process_user_range(p, ptr, len, true)) {
        return sys_ret(f, -EFAULT);
    }
    u32 avail = input_count();
    if (avail) {
        char tmp[64];
        u64 n = 0;
        while (n < len && n < sizeof(tmp)) {
            int c = input_pop();
            if (c < 0) {
                break;
            }
            tmp[n++] = (char)c;
        }
        if (!process_copy_to(p, ptr, tmp, n)) {
            return sys_ret(f, -EFAULT);
        }
        return sys_ret(f, (i64)n);
    }
    p->wait_buf = ptr;
    p->wait_len = len;
    p->wait_endpoint = 0;
    return sched_block_current(f, WAIT_CONSOLE);
}

static i64 sys_ipc_create(Process *p)
{
    Object *obj = object_create(OBJ_ENDPOINT);
    if (!obj) {
        return -ENOMEM;
    }
    int h = cap_grant(p, obj, RIGHT_SEND | RIGHT_RECV | RIGHT_TRANSFER);
    object_unref(obj);
    if (h < 0) {
        return -EMFILE;
    }
    return h;
}

static i64 sys_ipc_send(Process *p, u64 h, u64 ptr, u64 len)
{
    Object *obj = cap_get(p, h, OBJ_ENDPOINT, RIGHT_SEND);
    if (!obj) {
        return -EBADF;
    }
    if (len > IPC_MSG_MAX) {
        return -EMSGSIZE;
    }
    if (!process_user_range(p, ptr, len, false)) {
        return -EFAULT;
    }
    u8 msg[IPC_MSG_MAX];
    if (len && !process_copy_from(p, msg, ptr, len)) {
        return -EFAULT;
    }
    return endpoint_send(obj->u.endpoint, p->pid, msg, (u32)len);
}

static u64 sys_ipc_recv_block(TrapFrame *f, Process *p, u64 h, u64 ptr, u64 len)
{
    Object *obj = cap_get(p, h, OBJ_ENDPOINT, RIGHT_RECV);
    if (!obj) {
        return sys_ret(f, -EBADF);
    }
    if (len > IPC_MSG_MAX) {
        return sys_ret(f, -EMSGSIZE);
    }
    if (!process_user_range(p, ptr, len, true)) {
        return sys_ret(f, -EFAULT);
    }
    Endpoint *ep = obj->u.endpoint;
    if (ep->count > 0) {
        u8 msg[IPC_MSG_MAX];
        long n = endpoint_recv(ep, msg, (u32)len);
        if (n < 0) {
            return sys_ret(f, n);
        }
        if (!process_copy_to(p, ptr, msg, (u64)n)) {
            return sys_ret(f, -EFAULT);
        }
        return sys_ret(f, n);
    }
    if (ep->receiver && ep->receiver != p) {
        return sys_ret(f, -EBUSY);
    }
    ep->receiver = p;
    p->wait_endpoint = ep;
    p->wait_buf = ptr;
    p->wait_len = len;
    return sched_block_current(f, WAIT_IPC);
}

static i64 sys_spawn(Process *p, u64 name_ptr, u64 cap_handle)
{
    char name[PROC_NAME_MAX];
    memset(name, 0, sizeof(name));
    for (u32 i = 0; i < PROC_NAME_MAX - 1; i++) {
        if (!process_copy_from(p, &name[i], name_ptr + i, 1)) {
            return -EFAULT;
        }
        if (!name[i]) {
            break;
        }
    }
    if (!name[0]) {
        return -EINVAL;
    }
    Process *child = process_spawn(p, name, cap_handle);
    if (!child) {
        return -ENOMEM;
    }
    return (i64)child->pid;
}

static i64 sys_waitpid(Process *p, int pid, u64 status_ptr)
{
    Process *target = process_find_child(p, pid);
    if (!target) {
        return -ECHILD;
    }
    if (target->state == PROC_ZOMBIE) {
        int st = (int)target->exit_code;
        if (status_ptr && !process_copy_to(p, status_ptr, &st, sizeof(st))) {
            return -EFAULT;
        }
        int reaped = (int)target->pid;
        target->state = PROC_UNUSED;
        cap_close_all(target);
        if (target->pml4) {
            vmm_destroy_space(target->pml4);
        }
        memset(target, 0, sizeof(*target));
        return reaped;
    }
    if (target->state == PROC_UNUSED) {
        return -ECHILD;
    }
    p->wait_arg = (u64)(pid == -1 ? 0 : pid);
    p->wait_buf = status_ptr;
    return 0; /* caller blocks */
}

static i64 sys_info_fill(Process *p, u64 ptr)
{
    DeltaInfo info;
    memset(&info, 0, sizeof(info));
    info.magic = DELTA_INFO_MAGIC;
    info.version = DELTA_EPOCH;
    info.flags = process_boot_flags();
    info.ticks = timer_ticks();
    info.uptime_ms = timer_ms();
    info.total_frames = pmm_total_frames();
    info.free_frames = pmm_free_frames();
    info.process_count = (u64)process_count();
    info.running_pid = p->pid;
    info.console_writes = g_console_writes;
    info.context_switches = sched_context_switches();
    if (!process_copy_to(p, ptr, &info, sizeof(info))) {
        return -EFAULT;
    }
    return 0;
}

u64 syscall_dispatch(TrapFrame *frame)
{
    Process *p = process_current();
    if (!p) {
        frame->rax = (u64)-ENOSYS;
        return 0;
    }

    u64 nr = frame->rax;
    u64 a = frame->rdi;
    u64 b = frame->rsi;
    u64 c = frame->rdx;
    i64 ret = -ENOSYS;
    u64 switch_frame = 0;
    bool handled = false;

    switch (nr) {
    case SYS_EXIT:
        return process_exit_current(frame, (int)a);

    case SYS_WRITE:
        ret = sys_write(frame);
        break;

    case SYS_READ:
        switch_frame = sys_read_block(frame, p);
        if (switch_frame) {
            return switch_frame;
        }
        handled = true;
        break;

    case SYS_YIELD:
        p->slice = p->base_slice;
        switch_frame = sched_yield_current(frame);
        if (!switch_frame) {
            frame->rax = 0;
        }
        return switch_frame;

    case SYS_GETPID:
        ret = (i64)p->pid;
        break;

    case SYS_GETPPID:
        ret = (i64)p->parent_pid;
        break;

    case SYS_SLEEP: {
        u64 ms = a;
        u64 ticks = (ms * TIMER_HZ + 999) / 1000;
        if (ticks == 0) {
            ticks = 1;
        }
        p->wake_tick = timer_ticks() + ticks;
        p->wait_result = 0;
        switch_frame = sched_block_current(frame, WAIT_SLEEP);
        return switch_frame;
    }

    case SYS_TIME:
        ret = (i64)timer_ms();
        break;

    case SYS_IPC_CREATE:
        ret = sys_ipc_create(p);
        break;

    case SYS_IPC_SEND:
        ret = sys_ipc_send(p, a, b, c);
        break;

    case SYS_IPC_RECV:
        switch_frame = sys_ipc_recv_block(frame, p, a, b, c);
        if (switch_frame) {
            return switch_frame;
        }
        handled = true;
        break;

    case SYS_SPAWN:
        ret = sys_spawn(p, a, b);
        break;

    case SYS_WAITPID: {
        int pid = (int)a;
        ret = sys_waitpid(p, pid, b);
        if (ret == 0) {
            switch_frame = sched_block_current(frame, WAIT_CHILD);
            return switch_frame;
        }
        break;
    }

    case SYS_CLOSE:
        ret = cap_close(p, a) ? 0 : -EBADF;
        break;

    case SYS_DUP:
        ret = cap_dup(p, a, (u32)b);
        break;

    case SYS_IDLE:
        if (sched_other_runnable()) {
            switch_frame = sched_yield_current(frame);
            if (!switch_frame) {
                frame->rax = 0;
            }
            return switch_frame;
        }
        frame->rax = 0;
        __asm__ volatile("sti; hlt; cli" : : : "memory");
        return 0;

    case SYS_INFO:
        ret = sys_info_fill(p, a);
        break;

    case SYS_EVENTLOG: {
        u32 max = (u32)b;
        if (max > EVENT_MAX_BATCH) {
            max = EVENT_MAX_BATCH;
        }
        DeltaEvent ev[EVENT_MAX_BATCH];
        u32 n = event_copy(ev, max, true);
        if (n && !process_copy_to(p, a, ev, n * sizeof(DeltaEvent))) {
            ret = -EFAULT;
        } else {
            ret = (i64)n;
        }
        break;
    }

    case SYS_POWEROFF:
        event_log(EVT_POWER, p->pid, 0, 0);
        kprintf("[sys] poweroff requested by pid %u\n", p->pid);
        /* QEMU's isa-debug-exit device gives automated tests a deterministic
         * exit; on real hardware the ACPI PM1a control write follows.  The
         * debug-exit I/O region is 4 bytes wide, so use a 32-bit write. */
        outl(0x501, 1);
        outw(0x604, 0x2000);
        for (;;) {
            cpu_halt();
        }

    case SYS_BOOTINFO:
        ret = (i64)process_boot_flags();
        break;

    case SYS_CAP_COUNT:
        ret = cap_count(p);
        break;

    case SYS_CAP_INFO: {
        CapInfo ci;
        u32 rights = 0;
        Object *obj = 0;
        if (!cap_rights(p, a, &rights, &obj)) {
            ret = -EBADF;
            break;
        }
        ci.type = obj->type;
        ci.rights = rights;
        ci.id = obj->id;
        ret = process_copy_to(p, b, &ci, sizeof(ci)) ? 0 : -EFAULT;
        break;
    }

    case SYS_SET_WEIGHT: {
        u64 prio = a;
        if (prio > 14) {
            ret = -EINVAL;
            break;
        }
        p->priority = (u32)prio;
        p->base_slice = prio >= 10 ? 1 : (u32)(4 + (10 - prio));
        ret = 0;
        break;
    }

    default:
        ret = -ENOSYS;
        break;
    }

    if (!handled) {
        frame->rax = (u64)ret;
    }
    return 0;
}
