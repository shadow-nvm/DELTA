# DELTA Roadmap

**From v0.1 “First Light” to a usable, compatible operating-system platform.**

The ordering is deliberate: make the kernel robust and observable first, then
add services, then compatibility. Building Win32 or POSIX emulation before the
native object, memory, and process model is stable would freeze bad interfaces.

---

## v0.1 — First Light (current)

**Theme:** prove the core bet.

- [x] Custom BIOS stage 1 + stage 2, E820, long mode, identity paging
- [x] VGA text + UART + keyboard/serial interrupts
- [x] GDT/TSS, IDT, PIC, PIT
- [x] Physical frame allocator, kernel heap, 4-level paging
- [x] Per-process address spaces, NX/W^X user mappings
- [x] Ring 3 static ELF loader, capability table, spawn
- [x] Endpoint IPC, direct rendezvous, blocking receive
- [x] Priority/slice preemptive scheduler, sleep/wait/exit
- [x] 23 syscalls, interactive shell, selftest, event ledger
- [x] Automated QEMU test with deterministic poweroff

**Deliberately missing:** filesystem, network, SMP, UEFI, real drivers, user
heap, threads, signal delivery, shared memory, capability transfer in
messages, revocation trees, quotas, and any Windows/Linux ABI.

---

## v0.2 — Foundation hardening

**Goal:** make the existing core safe to build services on.

### Kernel correctness

- [ ] Double-fault IST and guard pages under kernel stacks
- [ ] Per-CPU data structures and a spinlock discipline (prep for SMP)
- [ ] TLB shootdown interface, even if only one CPU is used initially
- [ ] Page-fault handling for user demand-zero and `mmap`/`sbrk`
- [ ] Copy-on-write for shared memory objects
- [ ] User pointer hardening: SMAP/SMEP support, explicit `stac`/`clac`
- [ ] W^X and guard pages for the kernel heap
- [ ] Kernel stack canaries / red zones
- [ ] Capability table growth beyond 32 slots
- [ ] Object destructors and endpoint wakeup on close
- [ ] Zombie reaping without waiting for the next spawn
- [ ] More than 16 processes (dynamic table + PID allocator)
- [ ] Fault injection tests for allocation failure at every stage

### Syscall surface

- [ ] `mmap`, `munmap`, `mprotect`, `brk`
- [ ] Thread create/join/exit
- [ ] `yield_to`, `set_priority`, deadline hints
- [ ] `cap_revoke`, `cap_move`, capability transfer in IPC messages
- [ ] `endpoint_peer`, sender credentials/rights
- [ ] Event-log subscription/filtering
- [ ] Stable errno coverage and fuzzing of the syscall decoder

### Testing

- [ ] Unit tests for bitmap, heap, page-table walk, refcounts
- [ ] QEMU matrix: 128M/256M/1G RAM, serial and VGA
- [ ] Real-hardware smoke test on one x86-64 machine
- [ ] Boot-time randomized slab poisoning (debug builds)
- [ ] CI that builds and runs the selftest from a clean checkout

**Exit criterion:** no known memory-safety or context-switch bug; all kernel
objects have deterministic teardown; the selftest covers every syscall.

---

## v0.3 — Native services and storage

**Goal:** make DELTA usable as a small OS, not just a kernel.

- [ ] User-space service manager: spawn, supervise, restart, logs
- [ ] **Delta Store**: a versioned, content-addressed object store
  - immutable blobs, Merkle roots, atomic namespace updates
  - copy-on-write snapshots and rollback
- [ ] File service exporting file/directory objects
- [ ] Console service (move VGA/serial drivers out of ring 0)
- [ ] Device manager + PCI enumeration
- [ ] User-space driver framework for one device class (e.g. virtio-net or
  virtio-blk)
- [ ] Service manifest format and capability grants
- [ ] Persistent boot configuration and a real init system
- [ ] Time service, random service, logging service
- [ ] Native SDK: headers, linker script, `libdelta`, `hello` example

**Exit criterion:** boot to a service-managed shell with a persistent
read-only filesystem and a restartable console service.

---

## v0.4 — Networking and a real userspace

- [ ] virtio-net driver service
- [ ] Network service with packet/endpoint objects
- [ ] TCP/IP stack as an isolated service (not ring 0)
- [ ] Socket objects and capability-based bind/connect
- [ ] TLS library, DNS resolver, DHCP
- [ ] Native shell with pipelines/jobs
- [ ] User heap and dynamic loader for native services
- [ ] Shared memory and zero-copy IPC paths
- [ ] Threads and thread-local storage
- [ ] Crash dumps via the event ledger

**Exit criterion:** a native service can serve HTTP over QEMU user networking;
a service crash does not take down the shell.

---

## v0.5 — POSIX personality

**Goal:** run a meaningful subset of Linux binaries.

- [ ] POSIX personality server with process/session model
- [ ] `fork`, `execve`, `wait`, `kill`, signals
- [ ] File descriptors over native file objects
- [ ] `mmap`, `brk`, shared memory
- [ ] Pipes, Unix sockets, `poll`/`epoll` over endpoint waits
- [ ] Threads, mutexes, futex-like wait/wake
- [ ] A small libc port (musl or newlib) targeting the DELTA POSIX ABI
- [ ] Run coreutils/busybox in QEMU
- [ ] A Linux syscall translation layer for unmodified static binaries

**Exit criterion:** BusyBox shell and a static HTTP server run unmodified on
the POSIX personality.

---

## v0.6 — Windows personality

**Goal:** run a meaningful subset of Win32 console programs.

- [ ] NT object manager subset in userspace
- [ ] Handles, sections, events, mutants, semaphores
- [ ] ALPC-like local IPC over endpoints
- [ ] Win32 file/search APIs over the file service
- [ ] Registry service with transactional updates
- [ ] PE loader and DLL loading in the personality (not ring 0)
- [ ] Console subsystem
- [ ] A Windows API shim for console programs (Wine-style architecture, but
  capability-scoped and service-based)
- [ ] Run a simple Win32 console application

**Exit criterion:** a PE console binary runs without kernel changes; a
personality crash leaves the rest of the system alive.

---

## v0.7 — Graphics, audio, and interactivity

- [ ] Framebuffer object and display service
- [ ] GPU driver service (start with virtio-gpu)
- [ ] Window manager and input service
- [ ] Audio service (virtio-snd or Intel HDA)
- [ ] Native UI toolkit
- [ ] Compositor with per-window capability isolation
- [ ] Power management and ACPI
- [ ] UEFI boot path, retaining the BIOS path for testing

**Exit criterion:** a graphical native desktop with isolated application
windows and a restartable display service.

---

## v0.8 — Scale, security, and operations

- [ ] SMP boot and per-CPU scheduling
- [ ] CPU affinity and load balancing
- [ ] IOMMU and device isolation
- [ ] Capability revocation trees and quotas
- [ ] Per-service resource accounting
- [ ] Memory pressure and reclaim policy in user space
- [ ] Live service upgrade with capability handoff
- [ ] Secure boot chain and signed images
- [ ] Kernel self-tests on every boot
- [ ] Fuzzing, static analysis, and a threat model
- [ ] Formal verification of scheduler and capability invariants (ambitious)

---

## Feature parity map

| Windows feature | Linux feature | DELTA service / plan | Phase |
|---|---|---|---|
| NT executive | core kernel | native kernel | v0.1 |
| Win32 / POSIX | glibc / syscalls | personality servers | v0.5–0.6 |
| NTFS / ReFS | ext4 / btrfs | Delta Store + file service | v0.3 |
| Registry | `/etc` + config | transactional key-value service | v0.3 |
| WDM / KMDF | kernel modules | user-space driver services | v0.3+ |
| WinSock | BSD sockets | network service + socket objects | v0.4 |
| Direct3D / DXGI | DRM/KMS | display + GPU services | v0.7 |
| WASAPI | ALSA/PipeWire | audio service | v0.7 |
| Job objects | cgroups | capability quotas | v0.8 |
| Windows Update / MSI | package managers | content-addressed live upgrade | v0.3/v0.8 |
| ETW | ftrace/eBPF | event ledger + tracing service | v0.2+ |
| Hyper-V | KVM | not planned at kernel level | — |
| Secure Boot | IMA/DM-verity | signed image chain | v0.8 |

---

## Building an OS on DELTA

The kernel intentionally stops at mechanisms. A minimal OS on top looks like:

1. **Init/service manager** — first user process; spawns console, store, device
   manager, and shell; holds the bootstrap capabilities.
2. **Native SDK** — `<delta/syscall.h>` wrappers, capability conventions,
   service manifests, ELF linker script.
3. **Service protocol** — typed messages over endpoints; versioned; capabilities
   in messages (v0.2).
4. **File service** — Delta Store blobs plus a namespace service. Programs
   resolve names to capabilities once and pass handles.
5. **Driver services** — one process per device class, holding a device cap;
   supervised and restartable.
6. **Personalities** — POSIX and Win32 servers for third-party programs.
7. **Installer/updater** — content-addressed images; switch a namespace
   transactionally; keep the old image for rollback.
8. **Desktop** — display, input, window manager, audio, all services.

The kernel should not grow a filesystem or a socket API in ring 0. If a feature
needs policy, it belongs in a service.

---

## Risks and open questions

- **Performance of IPC-heavy designs.** Direct rendezvous helps, but zero-copy
  and shared memory are needed for drivers and networking.
- **Capability ergonomics.** Passing capabilities at spawn is easy; passing
  them in messages and revoking them safely is harder.
- **POSIX compatibility pressure.** A POSIX server will want signals, `fork`,
  and shared memory; designing these without kernel bloat is the main challenge.
- **Driver ecosystem.** No vendor will write a DELTA driver until DELTA has
  users; a compatibility driver host (e.g. virtio first) is the pragmatic path.
- **Security vs. usability.** Users expect “just run it”; capability prompts
  and manifests must be usable, not a permission maze.
- **Hardware coverage.** BIOS-first was right for a from-scratch proof; UEFI and
  ACPI are required for modern machines.
- **Verification.** The scheduler and capability invariants are small enough to
  model; doing so early would pay off.

---

## Definition of “usable”

A credible v1.0 would:

- boot on a modern UEFI machine with SMP;
- run a native shell, file service, network stack, and graphical desktop;
- run a meaningful subset of POSIX and Win32 console programs through isolated
  personalities;
- survive a driver or personality crash without rebooting;
- update services live and roll back atomically;
- expose a causal event ledger for every authority grant and failure;
- have a tested security model and a reproducible build.

That is the target. v0.1 is the first honest step, not the finish line.
