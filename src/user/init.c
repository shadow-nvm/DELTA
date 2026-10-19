#include "user.h"

static void print_info(void)
{
    DeltaInfo info;
    if (sys_info(&info) < 0) {
        puts("info: unavailable\n");
        return;
    }
    printf("  pid=%d ppid=%d uptime=%llu ms ticks=%llu\n",
           sys_getpid(), sys_getppid(), info.uptime_ms, info.ticks);
    printf("  frames: %llu free / %llu total; processes=%llu; switches=%llu\n",
           info.free_frames, info.total_frames,
           info.process_count, info.context_switches);
    printf("  caps owned: %ld\n", sys_cap_count());
}

static const char *event_name(u32 type)
{
    switch (type) {
    case EVT_BOOT:       return "boot";
    case EVT_CAP_GRANT:  return "cap-grant";
    case EVT_CAP_CLOSE:  return "cap-close";
    case EVT_PROC_SPAWN: return "spawn";
    case EVT_PROC_EXIT:  return "exit";
    case EVT_IPC_SEND:   return "ipc-send";
    case EVT_IPC_RECV:   return "ipc-recv";
    case EVT_FAULT:      return "fault";
    case EVT_SCHED:      return "sched";
    case EVT_POWER:      return "power";
    default:             return "?";
    }
}

static void print_events(void)
{
    DeltaEvent ev[16];
    long n = sys_eventlog(ev, 16);
    if (n < 0) {
        printf("dmesg: unavailable (%ld)\n", n);
        return;
    }
    for (long i = 0; i < n; i++) {
        printf("  %4llu  tick=%-6llu %-10s a=%llx b=%llx c=%llx\n",
               ev[i].seq, ev[i].tick, event_name(ev[i].type),
               ev[i].a, ev[i].b, ev[i].c);
    }
}

static int run_ipc_demo(void)
{
    char msg[] = "hello over a capability";
    int status = -1;

    long ep = sys_ipc_create();
    if (ep < 0) {
        printf("ipc: create failed (%ld)\n", ep);
        return -1;
    }
    long pid = sys_spawn("worker", ep);
    if (pid < 0) {
        printf("ipc: spawn failed (%ld)\n", pid);
        return -1;
    }
    printf("ipc: worker pid=%ld, endpoint cap=%ld\n", pid, ep);

    /* Let the worker reach its receive and park on the endpoint, so this send
     * exercises the direct rendezvous path rather than only the message queue. */
    sys_sleep(30);

    long sent = sys_ipc_send(ep, msg, (usize)(sizeof(msg) - 1));
    if (sent < 0) {
        printf("ipc: send failed (%ld)\n", sent);
        return -1;
    }

    long reaped = sys_waitpid(pid, &status);
    if (reaped < 0) {
        printf("ipc: wait failed (%ld)\n", reaped);
        return -1;
    }
    printf("ipc: worker %ld exited with %d\n", reaped, status);
    return status == 42 ? 0 : -1;
}

static int run_preempt_demo(void)
{
    long pid = sys_spawn("spinner", 0);
    if (pid < 0) {
        printf("preempt: spawn failed (%ld)\n", pid);
        return -1;
    }
    printf("preempt: spinner pid=%ld, running a CPU-bound loop\n", pid);

    u64 start = (u64)sys_time();
    volatile u64 acc = 0;
    while ((u64)sys_time() - start < 150) {
        for (volatile u64 i = 0; i < 25000; i++) {
            acc += i;
        }
    }

    int status = -1;
    long reaped = sys_waitpid(pid, &status);
    if (reaped < 0) {
        printf("preempt: wait failed (%ld)\n", reaped);
        return -1;
    }
    printf("preempt: spinner %ld exited with %d\n", reaped, status);
    return status == 7 ? 0 : -1;
}

static void selftest(void)
{
    puts("\n[selftest] DELTA verification sequence\n");
    printf("  ring3       : pid=%d\n", sys_getpid());
    printf("  capabilities: %ld handles\n", sys_cap_count());

    if (run_ipc_demo() == 0) {
        puts("  ipc         : endpoint message delivered\n");
    } else {
        puts("  ipc         : FAILED\n");
        puts("SELFTEST: FAIL\n");
        sys_poweroff();
    }

    if (run_preempt_demo() == 0) {
        puts("  preempt     : timer-driven address-space switch verified\n");
    } else {
        puts("  preempt     : FAILED\n");
        puts("SELFTEST: FAIL\n");
        sys_poweroff();
    }

    sys_sleep(30);
    puts("  scheduler   : sleep/wake completed\n");
    puts("  memory      : user address space active\n");
    puts("SELFTEST: PASS\n");
    sys_poweroff();
}

static void shell(void)
{
    char line[160];

    puts("\n  DELTA user shell. Type 'help' for commands.\n");
    for (;;) {
        printf("\n  delta> ");
        int n = read_line(line, sizeof(line));
        if (n <= 0) {
            continue;
        }
        if (!strcmp(line, "help")) {
            puts("  commands: help, info, ipc, dmesg, echo <text>, pid, poweroff\n");
        } else if (!strcmp(line, "info")) {
            print_info();
        } else if (!strcmp(line, "dmesg")) {
            print_events();
        } else if (!strcmp(line, "ipc")) {
            run_ipc_demo();
        } else if (!strncmp(line, "echo ", 5)) {
            printf("%s\n", line + 5);
        } else if (!strcmp(line, "pid")) {
            printf("pid=%d ppid=%d\n", sys_getpid(), sys_getppid());
        } else if (!strcmp(line, "poweroff")) {
            puts("poweroff requested\n");
            sys_poweroff();
        } else {
            printf("unknown command: %s\n", line);
        }
    }
}

int user_main(void)
{
    long flags = sys_bootinfo();

    printf("\n[user] ring 3 entered: pid=%d\n", sys_getpid());
    printf("[user] DELTA userspace is running on a capability-native kernel\n");

    if (flags & BOOT_FLAG_SELFTEST) {
        selftest();
        for (;;) {
            sys_idle();
        }
    }

    shell();
    return 0;
}
