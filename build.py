#!/usr/bin/env python3
"""DELTA build system.

The project deliberately avoids generated build systems: this script is the
single source of truth for compiling the boot loader, the kernel, the embedded
userspace, and the raw disk images.
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
BOOT = BUILD / "boot"
KERNEL = BUILD / "kernel"
USER = BUILD / "user"
TOOLS = ROOT / "tools"

ZIG_DIR = TOOLS / "zig-windows-x86_64-0.13.0"
NASM_DIR = TOOLS / "nasm-2.16.01"


def tool(name: str, env: str, candidates: list[Path]) -> str:
    value = os.environ.get(env)
    if value:
        return value
    found = shutil.which(name)
    if found:
        return found
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)
    raise SystemExit(f"error: could not find {name}; run tools/bootstrap.py or set {env}")


ZIG = tool("zig", "ZIG", [ZIG_DIR / "zig.exe", ZIG_DIR / "zig"])
NASM = tool("nasm", "NASM", [NASM_DIR / "nasm.exe", NASM_DIR / "nasm"])
QEMU = None
for _p in [
    Path(os.environ["QEMU"]) if os.environ.get("QEMU") else None,
    Path(r"C:\Program Files\qemu\qemu-system-x86_64.exe"),
    Path("/usr/bin/qemu-system-x86_64"),
]:
    if _p is not None and _p.is_file():
        QEMU = str(_p)
        break


CC_FLAGS = [
    "-target", "x86_64-freestanding-none",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-check",
    "-fno-stack-protector",
    "-fno-pic",
    "-fno-pie",
    "-mno-red-zone",
    "-mno-sse",
    "-mno-mmx",
    "-mno-80387",
    "-fno-asynchronous-unwind-tables",
    "-fno-unwind-tables",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-I", str(ROOT / "src" / "include"),
]


def run(cmd: list[str], cwd: Path = ROOT, quiet: bool = False) -> None:
    if not quiet:
        print("  " + " ".join(str(x) for x in cmd))
    proc = subprocess.run(
        [str(x) for x in cmd],
        cwd=str(cwd),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if proc.returncode != 0:
        print(proc.stdout)
        raise SystemExit(f"command failed ({proc.returncode}): {cmd[0]}")
    if proc.stdout.strip() and not quiet:
        print(proc.stdout.rstrip())


def ensure_dirs() -> None:
    for d in (BUILD, BOOT, KERNEL, USER, BUILD / "images"):
        d.mkdir(parents=True, exist_ok=True)


def compile_boot() -> None:
    run([NASM, "-f", "bin", "-o", BOOT / "boot.bin", ROOT / "src" / "boot" / "boot.asm"])
    run([NASM, "-f", "bin", "-o", BOOT / "stage2.bin", ROOT / "src" / "boot" / "stage2.asm"])
    run([NASM, "-f", "elf64", "-o", KERNEL / "entry.o", ROOT / "src" / "boot" / "entry.asm"])
    run([NASM, "-f", "elf64", "-o", KERNEL / "cpu.o", ROOT / "src" / "boot" / "cpu.asm"])
    run([NASM, "-f", "elf64", "-o", KERNEL / "isr.o", ROOT / "src" / "boot" / "isr.asm"])

    boot = (BOOT / "boot.bin").read_bytes()
    stage2 = (BOOT / "stage2.bin").read_bytes()
    if len(boot) != 512:
        raise SystemExit(f"boot sector is {len(boot)} bytes, expected 512")
    if len(stage2) > 16 * 512:
        raise SystemExit(f"stage2 is {len(stage2)} bytes, max is {16 * 512}")


def elf_load_image(path: Path) -> tuple[bytes, int]:
    """Convert a linked x86-64 ELF to the flat kernel image loaded at 1 MiB.

    File-backed PT_LOAD segments are emitted; .bss is intentionally omitted and
    zeroed by the kernel entry stub.  This keeps the boot image small even when
    the kernel has large static structures.
    """
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit(f"{path}: not a little-endian ELF64 file")
    e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x36)[0]
    e_phnum = struct.unpack_from("<H", data, 0x38)[0]
    e_entry = struct.unpack_from("<Q", data, 0x18)[0]

    segments = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", data, off)
        if p_type != 1:  # PT_LOAD
            continue
        p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from(
            "<QQQQQQ", data, off + 8
        )
        if p_filesz:
            segments.append((p_vaddr, p_offset, p_filesz, p_memsz, p_flags))
    if not segments:
        raise SystemExit(f"{path}: no loadable segments")

    base = 0x100000
    for vaddr, _, _, _, _ in segments:
        if vaddr < base:
            raise SystemExit(f"{path}: segment below kernel base: {vaddr:#x}")
    end = max(vaddr + filesz for vaddr, _, filesz, _, _ in segments)
    image = bytearray(end - base)
    for vaddr, offset, filesz, memsz, _ in segments:
        start = vaddr - base
        image[start:start + filesz] = data[offset:offset + filesz]
    return bytes(image), e_entry


def compile_user(name: str, crt0: Path, lib: Path) -> tuple[int, int]:
    src = ROOT / "src" / "user" / f"{name}.c"
    obj = USER / f"{name}.o"
    user_flags = [
        "-target", "x86_64-freestanding-none",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-check",
        "-fno-stack-protector",
        "-fno-pic",
        "-fno-pie",
        "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables",
        "-mno-red-zone",
        "-mcmodel=large",
        "-O2",
        "-Wall",
        "-Wextra",
        "-I", str(ROOT / "src" / "user"),
    ]
    run([ZIG, "cc", *user_flags, "-c", src, "-o", obj])
    out = USER / name
    if out.exists():
        out.unlink()
    run([
        ZIG, "build-exe", crt0, lib, obj,
        "-target", "x86_64-freestanding-none",
        "-fentry=_start",
        "-T", ROOT / "src" / "user.ld",
        "-fno-stack-check",
        "-fno-stack-protector",
        f"-femit-bin={out}",
    ])
    data = out.read_bytes()
    if data[:4] != b"\x7fELF":
        raise SystemExit(f"{name}: linker did not produce an ELF")
    entry = struct.unpack_from("<Q", data, 0x18)[0]
    if entry != 0x0000008000000000:
        raise SystemExit(f"{name}: entry is {entry:#x}, expected USER_BASE")
    target = out.with_suffix(".elf")
    if target.exists():
        target.unlink()
    out.rename(target)
    return len(data), entry


def compile_users() -> None:
    crt0 = USER / "crt0.o"
    lib = USER / "userlib.o"
    run([NASM, "-f", "elf64", "-o", crt0, ROOT / "src" / "user" / "crt0.asm"])
    user_flags = [
        "-target", "x86_64-freestanding-none",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-check",
        "-fno-stack-protector",
        "-fno-pic",
        "-fno-pie",
        "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables",
        "-mno-red-zone",
        "-mcmodel=large",
        "-O2",
        "-Wall",
        "-Wextra",
        "-I", str(ROOT / "src" / "user"),
    ]
    run([ZIG, "cc", *user_flags, "-c", ROOT / "src" / "user" / "userlib.c", "-o", lib])
    for name in ("init", "worker", "spinner", "idle"):
        size, entry = compile_user(name, crt0, lib)
        print(f"  {name}: {size} bytes at {entry:#x}")

    # The kernel embeds the final ELFs as .rodata symbols.
    run([NASM, "-f", "elf64", "-o", KERNEL / "blobs.o", ROOT / "src" / "boot" / "blobs.asm"])


def compile_kernel(extra_sources: list[Path] | None = None) -> tuple[int, int]:
    sources = sorted((ROOT / "src" / "kernel").glob("*.c"))
    if extra_sources:
        sources.extend(extra_sources)
    blobs = KERNEL / "blobs.o"
    if blobs.exists():
        sources.append(blobs)

    objdir = KERNEL / "obj"
    objdir.mkdir(parents=True, exist_ok=True)
    objects: list[Path] = []
    for src in sources:
        if src.suffix == ".o":
            objects.append(src)
            continue
        obj = objdir / (src.stem + ".o")
        run([ZIG, "cc", *CC_FLAGS, "-c", src, "-o", obj])
        objects.append(obj)
    objects.append(KERNEL / "entry.o")
    objects.append(KERNEL / "cpu.o")
    objects.append(KERNEL / "isr.o")

    out = KERNEL / "delta.elf"
    if out.exists():
        out.unlink()
    cmd = [
        ZIG, "build-exe",
        *objects,
        "-target", "x86_64-freestanding-none",
        "-fno-entry",
        "-T", ROOT / "src" / "kernel.ld",
        f"-femit-bin={out}",
    ]
    run(cmd)
    image, entry = elf_load_image(out)
    if entry != 0x100000:
        raise SystemExit(f"kernel entry is {entry:#x}, expected 0x100000")
    if len(image) > 0x70000:
        raise SystemExit(f"kernel image is {len(image)} bytes, max 0x70000")
    (KERNEL / "delta.bin").write_bytes(image)
    return len(image), entry


def build_image(name: str, test_mode: bool) -> Path:
    boot = bytearray((BOOT / "boot.bin").read_bytes())
    boot[508] = 1 if test_mode else 0
    stage2 = (BOOT / "stage2.bin").read_bytes().ljust(16 * 512, b"\0")
    kernel = (KERNEL / "delta.bin").read_bytes()
    checksum = sum(kernel) & 0xFFFFFFFFFFFFFFFF
    header = struct.pack("<IIQQQ", 0x4B544C44, 1, 0x100000, len(kernel), checksum)
    header = header.ljust(512, b"\0")
    payload = kernel + b"\0" * ((512 - len(kernel) % 512) % 512)
    image = bytes(boot) + stage2 + header + payload
    path = BUILD / "images" / name
    path.write_bytes(image)
    print(f"  image: {path} ({len(image)} bytes)")
    return path


def cmd_build(args: argparse.Namespace) -> None:
    ensure_dirs()
    print("[1/4] boot loader")
    compile_boot()
    print("[2/4] userspace")
    compile_users()
    print("[3/4] kernel")
    size, entry = compile_kernel()
    print(f"  kernel: {size} bytes, entry {entry:#x}")
    print("[4/4] disk images")
    build_image("delta.img", False)
    build_image("delta-test.img", True)


def cmd_clean(args: argparse.Namespace) -> None:
    if BUILD.exists():
        shutil.rmtree(BUILD)
    print("clean")


def cmd_run(args: argparse.Namespace) -> None:
    if not QEMU:
        raise SystemExit("error: QEMU not found")
    cmd_build(args)
    cmd = [
        QEMU,
        "-m", "256M",
        "-drive", f"format=raw,file={BUILD / 'images' / 'delta.img'}",
        "-boot", "c",
        "-serial", "stdio",
        "-no-reboot",
    ]
    print("\n  launching DELTA (Ctrl+Alt+G releases the mouse; guest poweroff exits QEMU)")
    print("  " + " ".join(cmd))
    subprocess.call(cmd, cwd=str(ROOT))


def cmd_test(args: argparse.Namespace) -> None:
    if not QEMU:
        raise SystemExit("error: QEMU not found")
    cmd_build(args)
    serial = BUILD / "serial.log"
    if serial.exists():
        serial.unlink()
    image = BUILD / "images" / "delta-test.img"
    cmd = [
        QEMU,
        "-m", "256M",
        "-drive", f"format=raw,file={image}",
        "-boot", "c",
        "-display", "none",
        "-serial", f"file:{serial}",
        "-device", "isa-debug-exit,iobase=0x501,iosize=4",
        "-no-reboot",
    ]
    print("  " + " ".join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, cwd=str(ROOT))
    try:
        out, _ = proc.communicate(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate()
        print(out or "")
        print((serial.read_text(errors="replace") if serial.exists() else ""))
        raise SystemExit("QEMU timed out")
    print(out or "")
    log = serial.read_text(errors="replace") if serial.exists() else ""
    print(log)
    if "DELTA KERNEL PANIC" in log:
        raise SystemExit("kernel panic")
    if "SELFTEST: PASS" not in log:
        raise SystemExit("self-test did not report PASS")
    print("test passed")


def main() -> None:
    parser = argparse.ArgumentParser(description="DELTA builder")
    parser.add_argument("command", nargs="?", default="build",
                        choices=["build", "run", "test", "clean"])
    parser.add_argument("--timeout", type=int, default=20)
    args = parser.parse_args()
    if args.command == "build":
        cmd_build(args)
    elif args.command == "run":
        cmd_run(args)
    elif args.command == "clean":
        cmd_clean(args)
    elif args.command == "test":
        cmd_test(args)


if __name__ == "__main__":
    main()
