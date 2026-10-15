#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Load/unload MCA twice on QEMU virt; no MCA hardware or ADSP probing."""
import argparse
import gzip
import re
import stat
import subprocess
import tempfile
from pathlib import Path


def module_order(root):
    modules = {}
    for entry in (root / "modules.order").read_text().splitlines():
        path = (root / entry).with_suffix(".ko")
        info = subprocess.check_output(
            ["readelf", "-p", ".modinfo", str(path)]).decode(errors="replace")
        name = re.search(r"\bname=([^\n]+)", info)[1].strip().replace("-", "_")
        deps = re.search(r"\bdepends=([^\n]*)", info)[1].strip()
        modules[name] = (path, {d.replace("-", "_") for d in deps.split(",") if d})
    selected = {name for name, (path, _) in modules.items()
                if "drivers/power/supply/mca" in path.as_posix()}
    if len(selected) != 52:
        raise RuntimeError(f"Expected 52 MCA modules, found {len(selected)}")
    order, visited, active = [], set(), set()

    def visit(name):
        if name in visited:
            return
        if name in active:
            raise RuntimeError(f"Module dependency cycle: {name}")
        active.add(name)
        for dependency in sorted(modules[name][1]):
            visit(dependency)
        active.remove(name)
        visited.add(name)
        order.append(name)

    for name in sorted(selected):
        visit(name)
    return modules, order


def initramfs(busybox, modules, order, keep_dependencies=False):
    dependencies = [name for name in order
                    if "drivers/power/supply/mca" not in modules[name][0].as_posix()]
    cycle = [name for name in order if name not in dependencies] if keep_dependencies else order
    if keep_dependencies:
        for name in dependencies:
            if modules[name][1] - set(dependencies):
                raise RuntimeError(f"External dependency {name} requires an MCA module")
    script = """#!/bin/sh
BB=/bin/busybox
$BB mount -t proc proc /proc
$BB mount -t sysfs sysfs /sys
$BB mount -t devtmpfs devtmpfs /dev
fail=0
"""
    if keep_dependencies:
        for name in dependencies:
            script += (f'echo "MCA-SMOKE DEPENDENCY {name}"\n'
                       f'$BB insmod /modules/{name}.ko || fail=1\n')
        script += "$BB cut -d ' ' -f1 /proc/modules > /baseline.modules\n"
    script += 'for round in 1 2; do\necho "MCA-SMOKE ROUND $round"\n'
    for name in cycle:
        script += (f'echo "MCA-SMOKE LOAD {name}"\n'
                   f'$BB insmod /modules/{name}.ko || fail=1\n')
    script += '[ -d /sys/class/xm_power/mca_event ] || fail=1\n'
    for name in reversed(cycle):
        script += (f'echo "MCA-SMOKE UNLOAD {name}"\n'
                   f'$BB rmmod {name} || fail=1\n')
    script += '[ ! -e /sys/class/xm_power ] || fail=1\n'
    if keep_dependencies:
        script += ("$BB cut -d ' ' -f1 /proc/modules | "
                   "$BB cmp /baseline.modules - || fail=1\n")
    else:
        script += '[ "$($BB wc -l < /proc/modules)" = 0 ] || fail=1\n'
    script += """done
if [ "$fail" = 0 ]; then
 echo "MCA-SMOKE PASS: 52 MCA modules, two load/unload cycles"
else
 echo "MCA-SMOKE FAIL"
fi
$BB poweroff -f
"""
    archive = bytearray()
    inode = 0

    def add(name, data, mode):
        nonlocal inode
        inode += 1
        encoded = name.encode() + b"\0"
        fields = [inode, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(encoded), 0]
        archive.extend(b"070701" + "".join(f"{v:08x}" for v in fields).encode())
        archive.extend(encoded)
        archive.extend(b"\0" * (-len(archive) % 4))
        archive.extend(data)
        archive.extend(b"\0" * (-len(archive) % 4))

    for directory in ["bin", "proc", "sys", "dev", "modules"]:
        add(directory, b"", stat.S_IFDIR | 0o755)
    add("bin/busybox", busybox.read_bytes(), stat.S_IFREG | 0o755)
    add("bin/sh", b"busybox", stat.S_IFLNK | 0o777)
    add("init", script.encode(), stat.S_IFREG | 0o755)
    for name in order:
        add(f"modules/{name}.ko", modules[name][0].read_bytes(), stat.S_IFREG | 0o644)
    add("TRAILER!!!", b"", 0)
    return gzip.compress(archive, compresslevel=1, mtime=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-out", type=Path, required=True)
    parser.add_argument("--image", type=Path,
                        help="Separate GKI Image when kernel-out contains vendor modules")
    parser.add_argument("--busybox", type=Path, required=True,
                        help="Static AArch64 BusyBox binary")
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--keep-dependencies", action="store_true",
                        help="Keep platform dependencies loaded; cycle all 52 MCA modules")
    args = parser.parse_args()
    root = args.kernel_out.resolve()
    image = args.image.resolve() if args.image else root / "arch/arm64/boot/Image"
    modules, order = module_order(root)
    with tempfile.TemporaryDirectory(prefix="mca-qemu-") as temp:
        ramdisk = Path(temp) / "initramfs.cpio.gz"
        ramdisk.write_bytes(initramfs(args.busybox, modules, order, args.keep_dependencies))
        command = [args.qemu, "-machine", "virt,accel=tcg", "-cpu", "cortex-a57",
                   "-smp", "2", "-m", "1024", "-nographic", "-nodefaults",
                   "-serial", "stdio", "-monitor", "none", "-nic", "none",
                   "-kernel", str(image),
                   "-initrd", str(ramdisk), "-append",
                   "console=ttyAMA0 rdinit=/init panic=-1", "-no-reboot"]
        with args.log.open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=150)
    output = args.log.read_text(errors="replace")
    if (result.returncode or "MCA-SMOKE PASS:" not in output
            or re.search(r"Oops:|BUG:|WARNING:|Unknown symbol|insmod:|rmmod:", output)):
        raise SystemExit(f"QEMU module smoke failed; see {args.log}")
    print(f"Verified two cycles: 52 MCA modules, {len(order)} with dependencies; "
          f"platform dependencies {'retained' if args.keep_dependencies else 'unloaded'}; "
          "no device probing")


if __name__ == "__main__":
    main()
