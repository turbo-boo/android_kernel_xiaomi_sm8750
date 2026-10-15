#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Check Dada module packaging and DT bindings against compiled MCA modules."""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser()
parser.add_argument("--kernel-out", type=Path)
parser.add_argument("--dt-root", type=Path)
args = parser.parse_args()

sun = (ROOT / "sun.bzl").read_text()
dada = (ROOT / "dada.bzl").read_text()
modules = re.findall(r'"(drivers/power/supply/mca/[^"\n]+\.ko)"', sun)
replacements = dict(re.findall(
    r'^\s*"(drivers/power/supply/mca/[^"\n]+\.ko)"\s*:\s*'
    r'"(drivers/power/supply/mca/[^"\n]+\.ko)"\s*,?$', dada, re.M))
extras = re.search(r'_dada_mca_extra_modules\s*=\s*\[(.*?)\]', dada, re.S)[1]
modules = [replacements.get(p, p) for p in modules]
modules += re.findall(r'"(drivers/power/supply/mca/[^"\n]+\.ko)"', extras)
assert len(modules) == len(set(modules)), "Duplicate packaged MCA output"
assert not any("_compat.ko" in p for p in modules), "Obsolete compatibility module"

fixture = json.loads((Path(__file__).parent / "dada_dt_contract.json").read_text())
compatibles = set(fixture["compatibles"])
if args.dt_root:
    dt = "\n".join((args.dt_root / p).read_text() for p in fixture["files"])
    declared = set(re.findall(r'compatible\s*=\s*"([^"]+)"', dt))
    assert compatibles <= declared, f"DT fixture missing: {compatibles - declared}"

source = "\n".join(p.read_text() for p in (ROOT / "drivers/power/supply/mca").rglob("*.c"))
bound = set(re.findall(r'\.compatible\s*=\s*"([^"]+)"', source))
assert compatibles <= bound, f"Missing Dada OF match: {compatibles - bound}"

if args.kernel_out:
    aliases = set()
    dependencies = {}
    for p in modules:
        ko = args.kernel_out / p
        assert ko.is_file(), f"Missing packaged module: {p}"
        info = subprocess.check_output(["readelf", "-p", ".modinfo", str(ko)], text=True)
        aliases.update(re.findall(r'alias=of:N[^\n]*?C([^\s]+?)C\*', info))
        name = re.search(r'name=([^\n]+)', info)[1]
        dependencies[name] = set(re.search(r'depends=([^\n]*)', info)[1].split(',')) - {''}
    complete = set()
    active = []

    def check_dependencies(name):
        assert name not in active, f"MCA module dependency cycle: {active + [name]}"
        if name in complete:
            return
        active.append(name)
        for dependency in dependencies[name] & dependencies.keys():
            check_dependencies(dependency)
        active.pop()
        complete.add(name)

    for name in dependencies:
        check_dependencies(name)
    assert compatibles <= aliases, f"Missing built OF alias: {compatibles - aliases}"
    symvers = args.kernel_out / "Module.symvers"
    assert symvers.is_file() and symvers.stat().st_size, "Full kernel Module.symvers required"
    print(f"Verified {len(modules)} packaged modules and {len(compatibles)} compiled DT bindings")
else:
    print(f"Verified {len(modules)} module paths and {len(compatibles)} source DT bindings")
