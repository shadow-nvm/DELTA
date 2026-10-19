# DELTA Compatibility Strategy

**How DELTA intends to provide “the good parts of Windows and Linux” without
copying either kernel — and what is actually implemented today.**

---

## 1. First, separate the kernel from the operating system

Windows and Linux are not single programs. Their visible features are spread
across many layers:

- **Windows:** NT executive, Win32, .NET, COM, the registry, the object
  manager, the driver model, the graphics stack, MSI, Defender, and so on.
- **Linux:** the kernel, glibc, the VFS, systemd, Wayland/X11, package
  managers, containers, and thousands of drivers.

A new kernel cannot become “all of Windows and Linux” by copying their ring-0
code. That would reproduce the coupling and attack surface that make those
systems hard to change. DELTA instead defines a small native substrate and a
**personality-server** model:

```
                 +--------------------------------------+
                 |  Windows personality  | POSIX        |
                 |  (NT/Win32 server)    | personality  |
                 +--------------------------------------+
                 |  native services: fs, net, graphics  |
                 |  user-space drivers                   |
                 +------------------ endpoints ----------+
                 |  DELTA kernel: objects, caps, IPC,    |
                 |  memory, scheduling, interrupts, time |
                 +--------------------------------------+
```

A personality is a user-space server that holds capabilities to native
services and exposes the ABI its programs expect. It can be restarted,
replaced, or run alongside another personality. It is not part of the trusted
computing base unless a program explicitly trusts it.

This is the answer to “contains every feature”: **the platform contains the
features through replaceable services, while the kernel contains only the
mechanisms those services need.** It is not a claim that v0.1 already has
those services.

---

## 2. Feature map

The table below maps well-known Windows/Linux facilities to DELTA mechanisms.
“Implemented” means present in v0.1; “design” means the interface exists or is
specified; “roadmap” means it is planned but not built.

| Facility | Windows / Linux analogue | DELTA mechanism | v0.1 status |
|---|---|---|---|
| Processes | `CreateProcess`, `fork/exec` | process object + static ELF loader + `spawn` | implemented |
| Threads | `CreateThread`, pthreads | multiple execution contexts per address space | roadmap |
| Scheduling | Win32 priorities, CFS/EEVDF | priority bands, slices, timer accounting | implemented (single CPU) |
| Virtual memory | `VirtualAlloc`, `mmap` | per-process page tables, `vmm_map_*` | implemented |
| User heap | HeapAlloc, `malloc` | demand-grown user region + `sbrk`/`mmap` | roadmap (`sbrk` reserved) |
| IPC | ALPC, pipes, Unix sockets | endpoint objects, capability transfer | implemented (basic) |
| Shared memory | file mappings, `shm` | memory objects + capability mapping | design |
| Files/dirs | NTFS/ReFS, ext4/btrfs | filesystem service exporting file objects | design |
| Registry/config | registry, `/etc` | versioned key-value service + capability-scoped namespaces | design |
| Devices | WDM/KMDF, Linux drivers | user-space driver services with device caps | design |
| Networking | Winsock, BSD sockets | network service + socket objects | roadmap |
| Graphics | D3D/DXGI, DRM/KMS | display/GPU services | roadmap |
| Audio | WASAPI, ALSA/PipeWire | audio endpoint services | roadmap |
| Security | tokens/ACLs, uid/gid + LSM | capability rights + object ACLs | capabilities implemented; ACLs design |
| Containers | Job objects, namespaces/cgroups | capability namespaces + quotas | design |
| Compatibility | Win32/NT, POSIX/ELF | personality servers | design |
| Crash recovery | WER, systemd restart | service supervisor + restartable drivers | design |
| Updates | MSI, package manager | content-addressed service images + live swap | design |
| Diagnostics | ETW, ftrace/perf | event ledger | basic implemented |

“Design” is not a euphemism for “done.” It means the kernel’s mechanisms are
deliberately shaped to support the item, but the service has not been written.

---

## 3. Drawbacks DELTA is designed to address

### 3.1 Ambient authority and root

**Problem.** Unix `root` and Windows Administrator are coarse, ambient
authorities. A compromised service often inherits far more power than it needs;
privilege separation is bolted on with namespaces, sandboxes, tokens, and LSMs.

**DELTA answer.** Authority exists only in capability slots. A process can act
only on objects for which it holds a capability with the required right. There
is no uid 0, no global `CAP_SYS_ADMIN`, and no “open by path then check” race:
the handle is the authority. Capabilities can be duplicated with fewer rights
and transferred explicitly at spawn.

**v0.1:** implemented for console, endpoints, and spawn inheritance. Object
ACLs, revocation trees, and per-object quotas are future work.

### 3.2 Global namespaces and registry rot

**Problem.** Linux has a single path namespace plus many ad-hoc namespaces;
Windows has drive letters, the registry, and the object manager. Global names
create collisions, make rollback hard, and turn configuration into a shared
mutable database.

**DELTA answer.** The kernel has **no path namespace and no registry**. Names
are resolved by user-space directory services that hand back capabilities.
Different personalities can see different namespace graphs; a service can be
given a private view without a kernel `chroot` or namespace implementation.

**v0.1:** no namespace service yet; the shell uses only built-in program names
and capability handles. The absence of a global *path* namespace is
intentional; PIDs exist for lifecycle accounting and are not authority tokens.

### 3.3 Monolithic drivers and reboots

**Problem.** A faulty kernel driver can crash or compromise the whole machine,
and updating core components often requires a reboot.

**DELTA answer.** Drivers are services holding device capabilities, isolated by
page tables. A crashed driver is a dead process; its supervisor can restart it.
A new driver version can be started, handed the device capability, and the old
one retired. The kernel never links a vendor blob.

**v0.1:** the model is documented and the isolation primitives exist; there is
no PCI/USB device manager or user-space driver framework yet.

### 3.4 Compatibility bloat

**Problem.** Linux carries 30+ years of syscall and driver ABI compatibility;
Windows carries enormous Win32/NT compatibility layers. Both grow the TCB and
make security review harder.

**DELTA answer.** Compatibility is a server. A Windows program talks to a
Win32/NT personality; a Linux program talks to a POSIX personality. Neither
personality is privileged by default, and a broken compatibility layer cannot
directly corrupt the kernel. Multiple personalities can coexist, and old ones
can be retired without touching ring 0.

**v0.1:** native ABI only.

### 3.5 Opaque failures

**Problem.** Diagnosing a crash often means correlating logs across kernel,
driver, init, and application layers, with no common causal identifier.

**DELTA answer.** The kernel records a causal event ledger: capability grants,
spawns, exits, IPC sends, faults, and power transitions. The ledger is a
first-class syscall, not a debug build afterthought.

**v0.1:** 128-entry ring and `dmesg`; no filtering, aggregation, or stable
export format yet.

### 3.6 Scheduling and isolation policy baked into the kernel

**Problem.** Linux exposes multiple scheduler classes and cgroup controllers;
Windows has job objects and priority classes. Both accumulate policy in ring 0.

**DELTA answer.** The kernel provides execution contexts and a minimal
priority/slice mechanism; richer policies (budgets, deadlines, quotas) can be
implemented by a scheduling service using capabilities and accounting
interfaces.

**v0.1:** fixed priority bands. Budget/deadline scheduling is roadmap, not a
current feature.

### 3.7 Update and rollback

**Problem.** System updates replace shared files and libraries in place,
creating dependency conflicts and hard rollback.

**DELTA answer.** Services are content-addressed immutable images. A new image
is started, capabilities are transferred, and the old image is retired. Because
the kernel has no global library namespace, there is no “DLL hell” at ring 0.

**v0.1:** design only. The embedded ELF images are statically linked, which is a
small step in that direction.

---

## 4. What “every feature” can honestly mean

It cannot mean “v0.1 already runs Windows and Linux software.” It means the
platform is designed so that the features can be supplied by replaceable
services rather than hard-coded into the kernel. Concretely:

1. **Native DELTA applications** use the capability ABI directly.
2. **A POSIX personality** implements `fork`, `execve`, `open`, `read`, `write`,
   `mmap`, signals, and a libc on top of native process, memory, file, and
   endpoint objects.
3. **A Windows personality** implements the NT syscall surface and Win32 on top
   of the same primitives, including handles, sections, ALPC-like IPC, and a
   registry service.
4. **Compatibility is composable.** A POSIX process can hold a capability to a
   Windows file service; a Windows process can use a native network endpoint.
5. **Failure is contained.** The personality can crash and restart without
   taking down the kernel or unrelated services.

This is a multi-year engineering path, not a switch that can be flipped. The
roadmap sequences it.

---

## 5. Honest comparison

DELTA v0.1 is not faster, more compatible, or more secure in practice than a
mature Linux or Windows kernel. It has not been tested on real hardware, has no
filesystem or network, and cannot run third-party binaries.

Its contribution is architectural: a working capability-native core with
explicit authority, isolated user mode, real IPC, real scheduling, and an
observability primitive — plus a compatibility strategy that does not require
copying the mistakes of the systems it aims to improve on.

The measure of success is not “does it have more features than Linux today.”
It is “can new features be added as isolated services, with authority
explicitly granted and failures contained?” v0.1 is the first evidence that the
answer can be yes.
