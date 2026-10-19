# DELTA Architecture

Version 0.1.0 “First Light”  
Target: x86-64, legacy BIOS, single CPU, QEMU-verified

This document describes what DELTA *is*, what it deliberately is not, and how
the implemented mechanisms fit together. It is an engineering document, not a
sales brochure: every “implemented” claim corresponds to code in `src/` and to
a boot that can be reproduced with `python build.py test`.

---

## 1. Goals and non-goals

### Goals

1. **A new kernel from first principles.** No Linux, Windows, Multiboot, GRUB,
   or borrowed kernel code. The boot sector, long-mode transition, page
   tables, object model, scheduler, and syscall ABI are DELTA’s own.
2. **Capability-native design.** No ambient authority, no global root, no
   global integer names for resources.
3. **Small privileged core.** Filesystems, device drivers, compatibility
   layers, and policy belong in ring 3. Ring 0 supplies mechanisms.
4. **Replaceable services.** A service crash or upgrade should be a user-space
   event, not a kernel panic or reboot.
5. **A credible path to Windows and Linux compatibility.** Personalities are
   user-space servers over native objects, not in-kernel emulation.
6. **Observable behaviour.** The kernel records a causal event ledger and
   exposes it to userspace.

### Non-goals for v0.1

- Binary compatibility with Windows or Linux.
- A production filesystem, network stack, GPU stack, or driver model.
- SMP, NUMA, real-time guarantees, or formal verification.
- UEFI, ACPI enumeration, power management, or hot-plug.
- A POSIX libc or a Windows API implementation.

Those are roadmap items, not hidden claims.

---

## 2. The DELTA thesis

Windows and Linux are not just kernels; they are enormous integrated
platforms. Most of what users call “features” live above the kernel: the
registry, Win32, DLL loading, the POSIX VFS, systemd, package managers,
graphics stacks, and driver models. Trying to copy all of that into a new ring
0 recreates the same coupling, attack surface, and stagnation.

DELTA separates three things:

```
        +--------------------------------------------------+
        |  Personalities: Win32 / POSIX / native services   |  ring 3
        |  filesystems, network, graphics, device drivers   |
        +-------------------- endpoints -------------------+
        |  Object-capability kernel                         |  ring 0
        |  objects | caps | address spaces | scheduling     |
        |  IPC | interrupts | time | event ledger           |
        +--------------------------------------------------+
```

- **Native mechanism** is universal: typed objects, capabilities, endpoints,
  address spaces, threads of execution, time, and interrupts.
- **Personality** is policy: a server that exposes `open/read/write`, or
  `CreateFileW`, or the NT syscall surface, by composing native objects.
- **Drivers** are services holding device capabilities. A bad driver can be
  killed and restarted; it cannot scribble over the kernel.

This is the central design bet. It is also why DELTA does not need to “contain
every feature” in ring 0 to eventually provide those features to programs.

---

## 3. Boot path

DELTA boots from a raw BIOS disk. There is no Multiboot header and no GRUB.

### Stage 1: `src/boot/boot.asm` (512 bytes)

- Loaded by the firmware at `0x7C00`.
- Sets up a real-mode stack and saves the boot drive.
- Reads 16 sectors from LBA 1 to `0x7E00` using INT 13h extended reads.
- Jumps to stage 2.
- Offset 508 of the sector is a build-time flag byte; offset 510 contains the
  `0xAA55` signature.

### Stage 2: `src/boot/stage2.asm` (≤ 16 sectors)

- Enables A20 through System Control Port A.
- Calls INT 15h `E820` and stores up to 64 entries at `0x9000`.
- Reads the kernel header at LBA 17 (`DLTK`, version, load address, size).
- Loads the flat kernel payload to a staging buffer at `0x10000`, then enters
  protected mode and copies it to `0x100000`.
- Builds a 4-level identity map for the first 1 GiB using 2 MiB pages:
  `PML4[0] -> PDPT[0] -> PD[512]`.
- Enables PAE, OSFXSR/OSXMMEXCPT, NXE, LME, and paging.
- Loads a temporary GDT and far-jumps into 64-bit code.
- Passes a `BootInfo` pointer in `RDI` and jumps to `0x100000`.

### Kernel entry: `src/boot/entry.asm`

- Sets a boot stack at `0x9F000`, clears direction flag.
- Zeroes `.bss` using linker symbols `__bss_start` and `__bss_end`.
- Calls `kmain(0xB000)`.

### Physical memory at entry

```
0x00000000  real-mode IVT/BDA/BIOS
0x00007C00  stage 1
0x00007E00  stage 2 (up to 0x9E00)
0x00009000  E820 entries
0x0000B000  BootInfo
0x00080000  page tables (PML4/PDPT/PD)
0x0009F000  boot stack top (grows down)
0x00100000  linked kernel image
...
0x02000000  kernel heap (16 MiB, reserved from the PMM)
```

The kernel is linked at 1 MiB and loaded as a flat image. `.bss` is not in the
file; the entry stub zeroes it.

---

## 4. Physical memory

`src/kernel/pmm.c` manages the first 1 GiB with a 4 KiB frame bitmap:
262,144 frames, 32 KiB of bitmap. The bootloader’s 2 MiB identity map makes
every managed frame directly addressable by the kernel.

Initialization:

1. Mark every frame used.
2. Walk the E820 map. For each usable range, clamp to `[1 MiB, 1 GiB]`, align
   inward, and mark frames free.
3. Reserve the first 1 MiB, the kernel image through `__kernel_end`, the VGA
   aperture, the boot structures, and the page tables.
4. Reserve the 16 MiB heap at physical `0x02000000`.

The allocator is a first-fit bitmap scan with a moving hint. It is protected
against interrupt re-entry with `irq_save`/`irq_restore`. It is not SMP-safe,
because v0.1 is single-CPU.

### Kernel heap

`src/kernel/heap.c` is a boundary-tag free-list allocator over the reserved
16 MiB region:

```
[ 32-byte header | payload ... ][ header | payload ... ]
```

- 16-byte alignment.
- Allocation splits a free block if at least `sizeof(header) + 32` bytes remain.
- Free inserts into an address-ordered list and coalesces with both neighbours.
- `kmalloc`, `kzalloc`, `krealloc`, `kfree`.
- Interrupts are masked around allocator mutation because allocations can
  happen in syscall context and object destruction.

The kernel heap is for kernel objects, page-table metadata, endpoints, and
process objects. User memory is entirely separate.

---

## 5. Virtual memory and isolation

`src/kernel/paging.c` uses 4-level x86-64 paging.

### Kernel view

The bootloader maps the first 1 GiB with 2 MiB pages, supervisor-only. That
map is shared by every address space via `PML4[0]`. Kernel code, the kernel
heap, the VGA buffer, the boot structures, and all physical memory the kernel
touches are identity-mapped there.

### User view

User mappings live in **PML4 slot 1**, starting at:

```
USER_BASE       0x0000008000000000   (512 GiB)
USER_TOP        0x0000008040000000   (+1 GiB)
USER_STACK_TOP  0x0000008000800000   (base + 8 MiB)
```

Why slot 1? It is completely disjoint from the kernel’s low identity map, so a
user mapping cannot accidentally alias kernel data. The PML4 slot is also easy
to walk and destroy on process exit.

Each process gets:

- a fresh PML4 page;
- `PML4[0]` copied from the kernel root (shared supervisor mapping);
- private PDPT/PD/PT tables for the user range;
- ELF code/data pages mapped with segment permissions;
- 16 stack pages below `USER_STACK_TOP`, writable and NX;
- no mapping of another process’s user pages.

`vmm_destroy_space` frees only the user subtree and table pages; it never frees
the shared kernel entries.

### Safety details

- `vmm_user_range_ok` walks every page and checks present, user, and writable
  bits as required.
- User copies (`process_copy_from`, `process_copy_to`) validate the range and
  temporarily switch `CR3` if the target is not the current process. This is
  required for direct IPC delivery and for writing a child’s exit status into
  a blocked parent.
- ELF segments with `PF_X` are mapped executable (NX clear); writable
  non-executable segments and the stack get NX set. This is W^X for user code
  in v0.1, enforced by the loader.
- The page-fault handler kills a ring-3 process instead of panicking the
  kernel when the fault originates in user mode.

Known gaps: no SMAP/SMEP, no KPTI, no guard page beneath kernel stacks, no
copy-on-write, no demand paging, and no swap.

---

## 6. Objects, capabilities, and IPC

### Object model

`src/kernel/object.c` defines:

```c
typedef struct Object {
    u32 type;       // CONSOLE, ENDPOINT, PROCESS, MEMORY
    u32 refs;
    u64 id;         // monotonic identity, not an authority token
    union { Endpoint *endpoint; Process *process; } u;
} Object;
```

Objects are kernel-allocated and reference-counted with capabilities. A handle
is an index into the process’s `caps[32]` table; it is **not** a pointer and
carries no authority outside that table.

### Rights

```
READ      observe / receive
WRITE     mutate / emit
SEND      send to an endpoint
RECV      receive from an endpoint
EXEC      execute / use as code source
ADMIN     reserved for object-specific control
TRANSFER  may be duplicated/inherited
```

Every operation checks the object type and the required right. A capability
for a console with only `READ` cannot be used with `SYS_WRITE`, even if handle
1 happens to be “stdout” in another process.

### Initial authority

The kernel creates a single console object during `objects_init`. `init` is
granted three capabilities to it:

```
1  console  WRITE   stdout
2  console  WRITE   stderr
3  console  READ    stdin
```

This is explicit bootstrap authority, not ambient authority: the grants are
visible, countable, inspectable (`SYS_CAP_INFO`), and can be closed or
attenuated. `idle` gets no capabilities at all.

### Spawning and inheritance

`SYS_SPAWN(name, cap_handle)` creates a child from one of the embedded static
ELFs. The child receives:

- its own console capabilities (via the same explicit bootstrap path);
- a duplicate of the parent’s selected capability, if one is supplied;
- a fresh address space, kernel stack, PID, and trap frame.

The duplicate is capped to `SEND|RECV|TRANSFER`; the parent’s other rights are
not silently inherited. The first IPC handle lands at slot 4 for the demo
programs. This is the implemented form of *capability transfer at process
creation*; sending capabilities inside messages is roadmap work.

### Endpoints

`src/kernel/ipc.c` implements bounded message queues:

- 16 messages per endpoint, 128-byte payloads;
- sender PID recorded in the message;
- if a receiver is blocked on the endpoint, the message is delivered directly
  into its validated user buffer and the receiver is made runnable;
- otherwise the message is queued;
- a full queue returns `-EAGAIN` rather than blocking the sender;
- a blocked receiver records its endpoint and buffer on its `Process`; only
  one receiver may wait on an endpoint at a time in v0.1.

The direct-delivery path is important: it proves that a syscall running in the
sender’s address space can safely write another process’s buffer by switching
`CR3`, without exposing a kernel pointer to either process.

### Why this is not “just another microkernel”

Microkernels already use IPC and user-space servers. DELTA’s distinction is the
combination of:

- a *global* object-capability rule, not only IPC capabilities;
- explicit rights attenuation on every grant/duplicate;
- no global path or resource-handle namespace in the kernel; PIDs exist for
  lifecycle accounting but are not authority;
- capability transfer at spawn as the only inheritance mechanism;
- a built-in causal event ledger for grants, IPC, lifecycle, and faults;
- an explicit compatibility plan in which Windows/POSIX personalities are
  ordinary services, not privileged kernel subsystems.

---

## 7. Processes, traps, and scheduling

### Process representation

`src/kernel/process.c` owns a static table of 16 `Process` records. Each has:

- PID, state (`RUNNABLE`, `RUNNING`, `BLOCKED`, `ZOMBIE`);
- PML4 physical address;
- static 16 KiB kernel stack;
- saved trap-frame pointer when not running;
- priority, slice, base slice, CPU tick accounting;
- wait reason and wait arguments;
- parent PID, exit code;
- 32 capability slots.

Static kernel stacks keep the context-switch path simple and avoid allocation
in the scheduler. They are a known scalability limit and will become
guard-paged allocations in a later version.

### Trap frame

All entries share one layout, defined in `src/include/delta/interrupts.h`:

```
r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax
vector error rip cs rflags rsp ss
```

`src/boot/isr.asm` pushes 15 general registers, then the vector/error pair
already on the stack; the C dispatcher receives a pointer to this frame. The
common restore path:

1. optionally replaces `RSP` with the frame chosen by the scheduler;
2. pops the 15 registers;
3. skips vector/error;
4. `iretq`.

### First entry

`process_create` maps the ELF and stack, then crafts a frame on the process’s
kernel stack:

```
rip    = ELF entry (0x8000000000)
cs     = 0x23  (user code, DPL 3)
rflags = 0x202 (IF=1)
rsp    = USER_STACK_TOP
ss     = 0x1B  (user data, DPL 3)
```

`sched_start` loads the chosen process’s `CR3`, writes its kernel stack top to
`TSS.rsp0`, and calls `delta_enter_user`, which restores the frame and `iretq`s
to ring 3.

### Blocking and waking

Blocking is explicit:

```c
p->wait_buf = user_buffer;
p->wait_len = length;
return sched_block_current(frame, WAIT_IPC);
```

`sched_block_current` saves the frame pointer, marks the process `BLOCKED`,
picks the next runnable process, switches address space and TSS stack, and
returns the next frame pointer to the assembly common path.

Wakers run in the context that caused the wake (syscall or IRQ), update the
blocked frame’s `RAX` with the syscall result, copy data into the blocked
process’s buffer if needed, mark it `RUNNABLE`, and let the scheduler pick it
later. Wait reasons are:

```
WAIT_NONE
WAIT_IPC      endpoint receive
WAIT_CONSOLE  console read
WAIT_SLEEP    timer deadline
WAIT_CHILD    waitpid
```

### Scheduling policy

v0.1 uses priority bands plus explicit slices:

- lower numeric priority runs first (`init`/workers = 5, idle = 15);
- each process has a slice in timer ticks; when it reaches zero the process
  goes to the back of its priority band and the scheduler picks again;
- idle has one-tick slices and is selected only when nothing better is
  runnable;
- timer preemption is only performed when the interrupted context is ring 3.
  Kernel execution is never rescheduled in place. This avoids the ambiguity
  between a ring-0 trap frame (no SS/RSP pushed) and a ring-3 frame, and it
  keeps the first scheduler correct and auditable.

The idle process runs a tiny ring-3 loop of `SYS_IDLE`. `SYS_IDLE` uses
`sti; hlt; cli` when nothing else is runnable, so an idle machine consumes
almost no CPU. When an interrupt wakes a process, the idle loop notices at its
next syscall boundary and yields.

Known gaps: no SMP, no load balancing, no deadline scheduling, no priority
inheritance, no per-cgroup quotas, and no CPU affinity.

---

## 8. Syscall interface

Entry is `int 0x80` through a DPL-3 interrupt gate. The ABI is:

```
RAX = syscall number
RDI, RSI, RDX, R10, R8, R9 = arguments
RAX = result (negative errno on failure)
```

Numbers and structures are in `src/include/delta/syscall.h`; userspace mirrors
them in `src/user/user.h`.

Implemented calls:

| # | Name | Notes |
|---:|---|---|
| 1 | `exit` | marks zombie, wakes parent, schedules away |
| 2 | `write` | console capability + WRITE, validated user buffer |
| 3 | `read` | console capability + READ, blocks when empty |
| 4 | `yield` | reschedules |
| 5/21 | `getpid` / `getppid` | introspection only |
| 6 | `sleep` | millisecond sleep, timer wake |
| 7 | `time` | monotonic milliseconds |
| 8 | `ipc_create` | endpoint object + capability |
| 9/10 | `ipc_send` / `ipc_recv` | queue/rendezvous, blocking receive |
| 11 | `spawn` | embedded ELF, capability transfer |
| 12 | `waitpid` | blocks; child exit writes status |
| 13/14 | `close` / `dup` | capability table operations |
| 16 | `idle` | `hlt` or yield |
| 17 | `info` | memory/process/scheduler snapshot |
| 18 | `eventlog` | returns recent `DeltaEvent`s |
| 19 | `poweroff` | QEMU debug exit / ACPI |
| 20 | `bootinfo` | test-mode flags |
| 22/23 | `cap_count` / `cap_info` | capability introspection |
| 24 | `set_weight` | adjust self priority/slice |
| 15/25 | `debug`, `sbrk` | reserved; return `ENOSYS` |

A syscall that blocks returns the next trap-frame pointer to the assembly
restore path; a syscall that completes writes `frame->rax` and returns zero.
This distinction is the heart of the context-switch design.

---

## 9. Console, UART, and input

- **VGA text:** 80×25 at `0xB8000`, coloured characters, cursor via CRTC
  ports, scrolling with `memmove`-style row copying.
- **UART:** COM1 at `0x3F8`, 115200 8N1, FIFO enabled with a **1-byte trigger**
  (important: a 14-byte trigger turns small input into character-timeout
  interrupts, which the first implementation mishandled).
- **Keyboard:** PS/2 set-1 scancodes, shift/caps, IRQ1.
- **Serial RX:** IRQ4, reads the UART until `IIR` reports no pending interrupt,
  handles both “received data” and “character timeout.”
- Both input sources push into a single 512-byte ring. A blocked
  `WAIT_CONSOLE` process is woken on the first byte; the waker copies up to its
  remaining buffer capacity and sets `RAX` to the byte count.

The shell is a normal ring-3 program. It has no special kernel access; its only
privilege is the console capabilities granted at spawn.

---

## 10. Interrupts and exceptions

- `src/boot/isr.asm` generates stubs for vectors 0–47 and `0x80`.
- Exceptions with CPU-pushed error codes (8, 10–14, 17, 21, 29, 30) do not push
  a second error word.
- The common path calls `interrupt_dispatch`.
- The PIC is remapped to vectors `0x20–0x2F`; IRQ0, IRQ1, and IRQ4 are
  unmasked for the timer, keyboard, and UART.
- EOI is sent after the handler. Timer preemption happens after EOI.
- User-mode faults print the vector, error, RIP, CS, RFLAGS, RSP, and CR2, then
  terminate the process. Kernel-mode faults panic.

A double-fault IST is not configured yet; a kernel stack overflow can still
take the machine down. This is a deliberate v0.1 limitation, not a hidden one.

---

## 11. Event ledger

`src/kernel/event.c` holds 128 `DeltaEvent` records:

```
seq, tick, type, a, b, c
```

Events are emitted for boot, capability grants, process spawn/exit, IPC sends,
faults, and power transitions. `SYS_EVENTLOG` returns up to 16 of the most
recent records in chronological order; the shell’s `dmesg` prints them.

The ledger is not a full tracing system: it has no per-CPU buffers, no
filtering, no stable binary format, and no causal graph. It is the seed of the
observability design, not its final form.

---

## 12. Test strategy

`python build.py test`:

1. Rebuilds everything.
2. Patches boot-sector flag byte 508 to mark a test image.
3. Boots `delta-test.img` with `-device isa-debug-exit`.
4. `init` sees `BOOT_FLAG_SELFTEST`, runs `selftest()`, and prints
   `SELFTEST: PASS`.
5. Kernel writes `outl(0x501, 1)`, causing QEMU to exit.
6. The harness checks the serial log for `SELFTEST: PASS` and absence of a
   kernel panic.

The self-test exercises user mode, syscall entry, capability inspection,
spawn, capability inheritance, endpoint send, direct delivery, blocking
receive, child exit, `waitpid`, sleep wake, timer preemption between two
CPU-bound user processes, address-space switching, and the event ledger. The
interactive image is additionally tested with QEMU monitor `sendkey` and a
serial TCP socket.

The test harness is not a substitute for hardware testing. DELTA has only been
run under QEMU’s BIOS so far.

---

## 13. Invariants worth preserving

If you extend DELTA, keep these true:

1. A process can only name an object through a valid capability with the
   required right and type.
2. Kernel mappings are supervisor-only; user mappings are in PML4 slot 1 and
   carry `PAGE_USER`.
3. Every user pointer is range-checked before use; cross-address-space copies
   switch `CR3` and switch back.
4. Only ring-3 contexts are timer-preempted. Kernel trap frames are never
   reinterpreted as user frames.
5. `TSS.rsp0` is updated on every address-space/process switch before returning
   to ring 3.
6. `.bss` is zero before C runs; the flat boot image does not carry it.
7. A zombie’s address space is destroyed only when the current process is not
   the zombie.
8. The event ledger never takes a page fault on user memory; it stores plain
   scalar data.

---

## 14. Comparison in one table

| Concern | Linux | Windows | DELTA v0.1 |
|---|---|---|---|
| Resource naming | fd/path/global namespaces | handles + object manager + registry | capability slots only |
| Privilege | uid/gid + capabilities, root | tokens, integrity levels, admin | rights on every capability |
| Drivers | mostly in kernel | mostly in kernel | user-space services (roadmap) |
| Compat layers | ELF/POSIX built in | Win32/NT built in | replaceable personality servers (designed) |
| IPC | pipes, sockets, SysV, etc. | ALPC, named pipes, COM | endpoint objects + caps |
| Scheduling | CFS/EEVDF + classes | priority + fair share | priority bands + slices |
| Observability | ftrace/eBPF/perf | ETW | event ledger |
| Update model | package + reboot | MSI + reboot | restartable services (roadmap) |
| Current maturity | decades | decades | v0.1, QEMU-tested core |

DELTA v0.1 is not “better than Linux or Windows” in feature coverage. It is a
different bet about where complexity belongs, with a working proof of the
core bet.
