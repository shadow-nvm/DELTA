# Building an OS on DELTA

**Short answer: yes.** DELTA v0.1 is a kernel, not an operating system. It gives
you processes, address spaces, capabilities, IPC, scheduling, interrupts, a
console, and an event ledger. An OS is the layer above that: a service manager,
a program loader, storage, drivers, namespaces, and eventually Windows/POSIX
personalities.

This guide is the practical path from “boots to a shell” to “a small OS.”

---

## 1. What you already have

| Primitive | Where | What it gives an OS |
|---|---|---|
| Ring-3 processes | `src/kernel/process.c` | isolated programs |
| Static ELF loader | `src/kernel/elf.c` | native executables |
| Capability tables | `src/kernel/object.c` | least authority |
| Endpoint IPC | `src/kernel/ipc.c` | service protocols |
| Spawn + capability inheritance | `SYS_SPAWN` | service startup |
| Blocking wait/wake | `src/kernel/process.c` | event-driven services |
| Preemptive scheduler | `src/kernel/process.c` | concurrency |
| Console/UART | `src/kernel/console.c`, `serial.c` | shell and logs |
| Event ledger | `src/kernel/event.c` | debugging and auditing |
| Embedded user programs | `src/user/`, `src/boot/blobs.asm` | bootstrap userland |

What is missing for a usable OS: a **service manager**, a **general program
loader**, **persistent storage**, **device drivers**, **networking**, **threads**,
and **compatibility personalities**.

---

## 2. The shortest experiment: add a built-in program

Today the kernel’s program table is compiled into the image. This is enough to
prototype an OS without writing a filesystem first.

1. Write `src/user/hello.c`:

```c
#include "user.h"

int user_main(void)
{
    printf("[hello] pid=%d, capability-native userspace\n", sys_getpid());
    return 0;
}
```

2. Add it to the build list in `build.py`:

```python
for name in ("init", "worker", "spinner", "idle", "hello"):
    ...
```

3. Add the ELF symbols in `src/boot/blobs.asm`:

```asm
global hello_elf_start
global hello_elf_end
align 16
hello_elf_start:
    incbin "build/user/hello.elf"
hello_elf_end:
```

4. Add it to `g_programs` in `src/kernel/process.c`:

```c
{ "hello", hello_elf_start, hello_elf_end },
```

5. Spawn it from `init` or the shell:

```c
long pid = sys_spawn("hello", 0);
```

This is useful for experimentation, demos, and early services, but it is not a
general OS: only programs baked into the kernel image can run.

---

## 3. The first real OS milestone: load programs from a service

To run arbitrary programs, add a **memory-object-backed executable loader** and
a **store service**.

### Kernel additions

1. **Memory objects**
   - `OBJ_MEMORY`: a page-aligned, capability-addressable region.
   - Rights: `READ`, `WRITE`, `EXEC`, `TRANSFER`.

2. **`SYS_SPAWN_IMAGE(memory_cap, entry_offset, caps)`**
   - Validates the image is an ELF64.
   - Creates an address space and maps the image.
   - Returns a process capability or PID.
   - The caller needs `EXEC` on the image capability.

3. **Capability vectors**
   Current `SYS_SPAWN` transfers one capability. A real service manager needs
   to hand a child several capabilities (console, store, bootstrap endpoint).
   Add a small array of handles, or a single “bootstrap directory” endpoint
   through which the child requests capabilities.

4. **A reply/receive protocol**
   Add `SYS_IPC_CALL` (send + block for reply) or build request IDs on top of
   `SYS_IPC_SEND`/`SYS_IPC_RECV`.

### Userspace additions

1. **`init` as service manager**
   - Holds the bootstrap console/store capabilities.
   - Reads a manifest (compiled in initially, then from the store).
   - Spawns services with least authority.
   - Supervises exits and restarts failed services.

2. **`store` service**
   - Starts with a read-only initramfs blob or a virtio-blk device capability.
   - Exposes `lookup(name) -> memory capability`.
   - Later becomes the versioned Delta Store (content-addressed blobs,
     snapshots, rollback).

3. **`console` service**
   - Moves VGA/UART out of ring 0 once a user-space driver framework exists.
   - Exposes read/write as endpoint messages.

4. **`shell` service**
   - Resolves a command name through the store.
   - Spawns the image with the right capabilities.
   - Never gets raw device access.

---

## 4. Suggested OS boot flow

```
firmware
  -> DELTA kernel
       -> init (service manager, ring 3)
            -> store service
            -> console service
            -> device manager
            -> shell
            -> optional personality servers
```

`init` should own only the capabilities needed to bootstrap. Every other
capability should be passed down explicitly and attenuated. A service should
not inherit “all of init’s rights.”

---

## 5. Service template

A DELTA service is a normal ring-3 program that receives an endpoint capability
and loops on it:

```c
#include "user.h"

int user_main(void)
{
    char request[128];
    int ep = 4; /* capability passed at spawn in the current demo ABI */

    for (;;) {
        long n = sys_ipc_recv(ep, request, sizeof(request));
        if (n < 0) {
            continue;
        }
        printf("[service] request: %.*s\n", (int)n, request);
        /* process, then reply over a reply endpoint */
    }
}
```

For a real service API, define a small typed protocol:

```
struct request {
    u32 op;
    u32 request_id;
    u32 payload_len;
    u8  payload[];
};
```

Version the protocol. Do not put policy in the kernel; put it in the service.

---

## 6. Milestones

### Milestone A — richer native userland

- [ ] Split `init` into service manager + shell.
- [ ] Add a bootstrap endpoint protocol.
- [ ] Add a **service manifest** (compiled in first).
- [ ] Add service restart on exit.
- [ ] Add `dmesg` service/log subscription.
- [ ] Raise process limit beyond 16 and add a process-info service.

### Milestone B — executable loading

- [ ] Memory objects + `mmap`/`SYS_MAP`.
- [ ] `SYS_SPAWN_IMAGE` from a memory capability.
- [ ] Capability vector on spawn.
- [ ] Read-only initramfs service.
- [ ] Shell can run stored programs by name.

### Milestone C — persistence and drivers

- [ ] virtio-blk user-space driver.
- [ ] Delta Store: content-addressed blobs, snapshots, rollback.
- [ ] File service with directory capabilities.
- [ ] Device manager + PCI enumeration.
- [ ] Console service moved to ring 3.

### Milestone D — a real OS

- [ ] virtio-net + network service.
- [ ] Threads + user heap + dynamic loader.
- [ ] POSIX personality (BusyBox, static binaries).
- [ ] Windows personality (PE loader + Win32 console subset).
- [ ] Display/GPU/audio services.
- [ ] SMP and IOMMU.
- [ ] Live service upgrade.

---

## 7. Current constraints to design around

- **16 process slots**, 16 KiB kernel stacks. Fine for a prototype; not enough
  for a desktop. Expand before building many services.
- **One capability per spawn** (plus console bootstrap). A service manager
  needs a better bootstrap handoff.
- **No filesystem.** The store service is the first real storage abstraction.
- **No user heap.** Services are static ELFs with small fixed buffers; add
  `sbrk`/`mmap` before writing large services.
- **No threads.** Services are single-threaded event loops for now.
- **No shared memory.** IPC copies 128-byte messages; zero-copy comes later.
- **BIOS only.** UEFI is needed for modern hardware.
- **No SMP.** One CPU, one scheduler.
- **No dynamic linker.** Services are linked at `USER_BASE`; static ELF only.

None of these blocks a first OS. They shape the order of work.

---

## 8. What not to do

- Do **not** add a filesystem, socket stack, or Windows compatibility layer to
  ring 0. That recreates the monolith DELTA is trying to avoid.
- Do **not** give every service the console capability by default. Authority
  should be visible and minimal.
- Do **not** make the shell privileged. The shell is just another client with a
  console capability.
- Do **not** expose raw physical addresses or kernel pointers through handles.
  Handles are capability-table indices, not pointers.

---

## 9. Recommended next step

The smallest change that unlocks real OS work is:

1. add a **bootstrap endpoint** so a parent can hand a child a service protocol
   rather than a single ad-hoc capability;
2. add **`SYS_SPAWN_IMAGE`** backed by a memory object;
3. split `init` into a supervisor plus a shell;
4. add a read-only initramfs service.

That turns DELTA from “a kernel with a built-in demo” into “a kernel that boots
a small, extensible OS.”

See [ARCHITECTURE.md](ARCHITECTURE.md), [COMPATIBILITY.md](COMPATIBILITY.md),
and [ROADMAP.md](ROADMAP.md) for the design rationale and larger plan.
