#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Compare built MCA exports, aliases and wire calls with Dada stock evidence.

This checks names and observed wire arguments. Optional GKI CRC comparison
covers the stock core imports in the fixture, not structure layout, all vendor
imports or runtime compatibility.
"""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path
import re
import subprocess


def readelf(path, *args):
    return subprocess.check_output(["readelf", *args, str(path)], text=True)


def exported_symbols(path):
    exports = set()
    for line in readelf(path, "-Ws").splitlines():
        columns = line.split()
        # __ksymtab_strings and __ksymtab_gpl are sections, not exports.
        if (len(columns) == 8 and columns[3] != "SECTION" and
                columns[7].startswith("__ksymtab_")):
            exports.add(columns[7][len("__ksymtab_"):])
    return exports


def check_gki_crcs(symvers):
    fixture = json.loads((Path(__file__).parent / "dada_stock_gki_crc.json").read_text())
    core = {}
    for line in symvers.read_text().splitlines():
        columns = line.split()
        if len(columns) >= 3 and columns[2] == "vmlinux":
            core[columns[1]] = int(columns[0], 16)
    assert core, f"No core GKI exports in {symvers}"
    for name, crc in fixture["crcs"].items():
        assert name in core, f"Stock GKI import missing: {name}"
        assert core[name] == int(crc, 16), (
            f"Stock GKI CRC mismatch: {name}: {crc} != {core[name]:#010x}")
    print(f"Verified {len(fixture['crcs'])} stock core GKI import CRCs")


def register_value(operand, values):
    match = re.fullmatch(r"([xw])(\d+|zr)", operand)
    if not match:
        return None
    width, register = match.groups()
    value = 0 if register == "zr" else values.get(register)
    if value is not None:
        return value & ((1 << (32 if width == "w" else 64)) - 1)
    return None


def advance_registers(opcode, operands, values):
    """Evaluate the integer constant forms used by the recorded wire calls."""
    values = values.copy()
    args = [part.strip() for part in operands.split(",")]
    writeback = re.search(r"\[x(\d+)[^\]]*\](?:!|,\s*#)", operands)
    if writeback:
        values.pop(writeback[1], None)
    if opcode in {"bl", "blr"}:
        # AAPCS64 calls preserve x19-x29, not argument/temporary registers.
        return {reg: value for reg, value in values.items()
                if 19 <= int(reg) <= 29}
    destination = re.fullmatch(r"([xw])(\d+)", args[0])
    if not destination or opcode in {"cmp", "cmn", "tst", "cbz", "cbnz",
                                      "tbz", "tbnz"}:
        return values
    if opcode.startswith("st") and opcode not in {"stxr", "stlxr", "stxp", "stlxp"}:
        return values
    width, register = destination.groups()
    mask = (1 << (32 if width == "w" else 64)) - 1
    value = None
    if opcode == "mov" and len(args) == 2:
        if args[1].startswith("#"):
            value = int(args[1][1:], 0)
        else:
            value = register_value(args[1], values)
    elif opcode == "movk" and len(args) >= 2:
        shift = int(args[2].removeprefix("lsl #"), 0) if len(args) == 3 else 0
        previous = values.get(register)
        if previous is not None:
            value = ((previous & ~(65535 << shift)) |
                     (int(args[1][1:], 0) << shift))
    elif opcode in {"add", "sub"} and len(args) == 3 and args[2].startswith("#"):
        source = register_value(args[1], values)
        if source is not None:
            immediate = int(args[2][1:], 0)
            value = source + immediate if opcode == "add" else source - immediate
    if value is None:
        values.pop(register, None)
    else:
        values[register] = value & mask
    if opcode in {"ldp", "ldpsw"}:
        # Both destinations are overwritten, not just the first register.
        second = re.fullmatch(r"[xw](\d+)", args[1])
        if second:
            values.pop(second[1], None)
    return values


def direct_calls(instructions):
    """Join constants across local branches; conflicting paths stay unknown."""
    if not instructions:
        return []
    addresses = {instruction[0]: index
                 for index, instruction in enumerate(instructions)}
    incoming = {0: {}}
    pending = deque([0])
    while pending:
        index = pending.popleft()
        _, opcode, operands, target = instructions[index]
        values = advance_registers(opcode, operands, incoming[index])
        successors = []
        conditional = opcode.startswith("b.") or opcode in {
            "cbz", "cbnz", "tbz", "tbnz"}
        if (opcode not in {"b", "br", "ret", "eret", "brk"} and
                index + 1 < len(instructions)):
            successors.append(index + 1)
        if (opcode == "b" or conditional) and target is None:
            branch = operands.split(",")[-1].strip().split()[0]
            destination = addresses.get(int(branch, 16))
            if destination is not None:
                successors.append(destination)
        for successor in successors:
            previous = incoming.get(successor)
            joined = values if previous is None else {
                reg: value for reg, value in previous.items()
                if values.get(reg) == value}
            if previous is None or joined != previous:
                incoming[successor] = joined
                pending.append(successor)
    return [{"target": target, "property": incoming[index].get("0"),
             "event": incoming[index].get("1"), "size": incoming[index].get("2")}
            for index, (_, _, _, target) in enumerate(instructions)
            if target is not None and index in incoming]


def module_calls(path, objdump):
    """Record constant x0/x1/x2 arguments at AArch64 direct-call relocations.

    Constants can pass through callee-saved registers and immediate arithmetic.
    Caller-saved values are invalidated at calls, and branch joins retain only
    constants shared by every reachable path.
    """
    text = subprocess.check_output([objdump, "-dr", str(path)], text=True)
    functions = {}
    instructions = []
    for line in text.splitlines():
        match = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if match:
            instructions = functions.setdefault(match[1], [])
            continue
        match = re.search(r"R_AARCH64_(?:CALL26|JUMP26)\s+(\S+)", line)
        if match and instructions:
            instructions[-1][3] = match[1]
            continue
        match = re.match(
            r"^\s*([0-9a-f]+):\s+(?:(?:[0-9a-f]{8}|[0-9a-f]{2})\s+)+"
            r"([a-z][a-z0-9.]*)\s*(.*)", line)
        if match:
            address, opcode, operands = match.groups()
            instructions.append([int(address, 16), opcode,
                                 operands.split("//")[0].strip(), None])
    return {name: direct_calls(body) for name, body in functions.items()}


def wire_calls(path, objdump):
    functions = module_calls(path, objdump)

    def expand(name, visited):
        if name in visited:
            return []
        visited = visited | {name}
        found = []
        for call in functions.get(name, []):
            target = call["target"]
            for operation in ("read", "write"):
                if target in {f"mca_adsp_glink_{operation}_prop", f"adsp_{operation}"}:
                    found.append({"op": operation, "property": call["property"],
                                  "size": call["size"]})
                    break
            else:
                found.extend(expand(target, visited))
        return found

    return {name: expand(name, set()) for name in functions}


def event_calls(path, objdump):
    return {name: [{"type": call["property"], "event": call["event"]}
                   for call in calls if call["target"] == "mca_event_block_notify"]
            for name, calls in module_calls(path, objdump).items()}


def check_contract(fixture, modules, objdump, stock=False):
    exports_count = 0
    for row in fixture["modules"]:
        assert row["name"] in modules, f"Missing stock module name: {row['name']}"
        path = modules[row["name"]]
        if stock:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            assert digest == row["sha256"], f"{row['name']}: stock SHA-256 mismatch"
        exports = exported_symbols(path)
        missing = set(row["exports"]) - exports
        assert not missing, f"{row['name']}: missing stock exports {sorted(missing)}"
        aliases = set(re.findall(r"alias=([^\n]+)", readelf(path, "-p", ".modinfo")))
        missing = set(row["aliases"]) - aliases
        assert not missing, f"{row['name']}: missing stock aliases {sorted(missing)}"
        exports_count += len(row["exports"])
    disassembly = {}
    for row in fixture["wire"]:
        module = row["module"]
        if module not in disassembly:
            disassembly[module] = wire_calls(modules[module], objdump)
        function = row["function"] if stock else row["built_function"]
        calls = disassembly[module].get(function, [])
        expected = {key: row[key] for key in ("op", "property", "size")}
        assert any(all(expected[k] is None or call[k] == expected[k]
                       for k in expected) for call in calls), (
            f"{module}:{function}: expected {expected}, got {calls}")
    event_disassembly = {}
    for row in fixture.get("events", []):
        module = row["module"]
        if module not in event_disassembly:
            event_disassembly[module] = event_calls(modules[module], objdump)
        function = row["function"] if stock else row["built_function"]
        expected = {key: row[key] for key in ("type", "event")}
        calls = event_disassembly[module].get(function, [])
        assert expected in calls, (
            f"{module}:{function}: expected event {expected}, got {calls}")
    kind = "stock evidence" if stock else "compiled modules"
    print(f"Verified {kind}: {len(fixture['modules'])} stock module names, "
          f"{exports_count} exports, aliases, {len(fixture['wire'])} wire contracts "
          f"and {len(fixture.get('events', []))} event contracts")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kernel-out", type=Path)
    parser.add_argument("--stock-modules", type=Path,
                        help="Recheck fixture hashes and contracts against a stock .ko directory")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--gki-symvers", type=Path,
                        help="Compare stock core import CRCs with GKI Module.symvers")
    args = parser.parse_args()
    if not args.kernel_out and not args.stock_modules:
        parser.error("provide --kernel-out and/or --stock-modules")
    fixture = json.loads((Path(__file__).parent / "dada_stock_contract.json").read_text())
    if args.stock_modules:
        stock = {p.name: p for p in args.stock_modules.glob("*.ko")}
        check_contract(fixture, stock, args.objdump, stock=True)
    if args.kernel_out:
        built = {p.name: p for p in
                 (args.kernel_out / "drivers/power/supply/mca").rglob("*.ko")}
        check_contract(fixture, built, args.objdump)
    if args.gki_symvers:
        check_gki_crcs(args.gki_symvers)


if __name__ == "__main__":
    main()
