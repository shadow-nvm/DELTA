#!/usr/bin/env python3
"""Download the pinned DELTA build toolchain (Zig 0.13 + NASM 2.16).

QEMU is intentionally not downloaded here: install it through your platform's
package manager or from qemu.org, then point the QEMU environment variable at
qemu-system-x86_64 if it is not in a standard location.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"

ZIG_URL = "https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip"
ZIG_SHA256 = "d859994725ef9402381e557c60bb57497215682e355204d754ee3df75ee3c158"
NASM_URL = "https://www.nasm.us/pub/nasm/releasebuilds/2.16.01/win64/nasm-2.16.01-win64.zip"


def download(url: str, dest: Path) -> None:
    if dest.exists():
        print(f"  cached {dest.name}")
        return
    print(f"  downloading {url}")
    tmp = dest.with_suffix(dest.suffix + ".part")
    with urllib.request.urlopen(url, timeout=120) as response, open(tmp, "wb") as out:
        shutil.copyfileobj(response, out)
    tmp.replace(dest)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    TOOLS.mkdir(exist_ok=True)
    zig_zip = TOOLS / "zig.zip"
    nasm_zip = TOOLS / "nasm.zip"

    print("[1/3] Zig 0.13")
    download(ZIG_URL, zig_zip)
    digest = sha256(zig_zip)
    if digest != ZIG_SHA256:
        raise SystemExit(f"Zig checksum mismatch: {digest}")
    if not (TOOLS / "zig-windows-x86_64-0.13.0").exists():
        with zipfile.ZipFile(zig_zip) as z:
            z.extractall(TOOLS)

    print("[2/3] NASM 2.16.01")
    download(NASM_URL, nasm_zip)
    if not (TOOLS / "nasm-2.16.01").exists():
        with zipfile.ZipFile(nasm_zip) as z:
            z.extractall(TOOLS)

    print("[3/3] QEMU")
    qemu_candidates = [
        Path(r"C:\Program Files\qemu\qemu-system-x86_64.exe"),
        Path("/usr/bin/qemu-system-x86_64"),
        Path("/usr/local/bin/qemu-system-x86_64"),
    ]
    if any(p.exists() for p in qemu_candidates) or shutil.which("qemu-system-x86_64"):
        print("  QEMU found")
    else:
        print("  QEMU not found. Install it, then re-run the build or set QEMU=...")

    print("\nToolchain ready. Next: python build.py test")


if __name__ == "__main__":
    main()
