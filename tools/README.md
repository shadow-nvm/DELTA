# Local toolchain

The build uses a self-contained Zig 0.13 (compiler + linker + freestanding C
headers) and NASM 2.16. QEMU is expected to be installed separately.

Run:

```powershell
python scripts/bootstrap_tools.py
```

to download and extract the pinned tools into this directory, or provide
`ZIG` and `NASM` environment variables pointing at existing installations.

Layout expected by `build.py`:

```
tools/zig-windows-x86_64-0.13.0/zig.exe
tools/nasm-2.16.01/nasm.exe
```

On Linux/macOS, put `zig` and `nasm` on `PATH`; the same build script works.
