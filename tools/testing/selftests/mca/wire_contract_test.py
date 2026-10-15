#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Check wire argument recovery from real AArch64 relocatable objects."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from verify_stock_contract import event_calls, wire_calls


ASSEMBLY = """
.text
.global saved_property
.type saved_property, %function
saved_property:
    mov w20, #1
    movk w20, #2, lsl #16
    bl unrelated_provider
    add w0, w20, #11
    mov w2, #4
    bl mca_adsp_glink_read_prop
    ret
.size saved_property, .-saved_property

.global clobbered_property
.type clobbered_property, %function
clobbered_property:
    mov w9, #12
    movk w9, #2, lsl #16
    bl unrelated_provider
    mov w0, w9
    mov w2, #4
    bl mca_adsp_glink_read_prop
    ret
.size clobbered_property, .-clobbered_property

.global conflicting_paths
.type conflicting_paths, %function
conflicting_paths:
    cbz w3, 1f
    mov w0, #12
    movk w0, #2, lsl #16
    b 2f
1:
    mov w0, #4
2:
    mov w2, #4
    bl mca_adsp_glink_read_prop
    ret
.size conflicting_paths, .-conflicting_paths

.global overwritten_pair
.type overwritten_pair, %function
overwritten_pair:
    mov w20, #12
    movk w20, #2, lsl #16
    ldp x19, x20, [x1]
    mov w0, w20
    mov w2, #4
    bl mca_adsp_glink_read_prop
    ret
.size overwritten_pair, .-overwritten_pair

.global narrowed_property
.type narrowed_property, %function
narrowed_property:
    mov x20, #12
    movk x20, #2, lsl #16
    movk x20, #1, lsl #32
    mov w0, w20
    mov w2, #4
    bl mca_adsp_glink_write_prop
    ret
.size narrowed_property, .-narrowed_property

.global event_sender
.type event_sender, %function
event_sender:
    mov w0, #5
    mov w1, #42
    mov x2, xzr
    bl mca_event_block_notify
    ret
.size event_sender, .-event_sender
"""


class WireContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        objdump = next((tool for tool in (
            "llvm-objdump", "llvm-objdump-18", "aarch64-linux-gnu-objdump")
            if shutil.which(tool)), None)
        if not objdump:
            raise RuntimeError("Install LLVM or AArch64 binutils for wire contract tests")
        with tempfile.TemporaryDirectory(prefix="mca-wire-test-") as directory:
            obj = Path(directory) / "wire.o"
            subprocess.run(["clang", "--target=aarch64-linux-gnu", "-x", "assembler",
                            "-c", "-o", str(obj), "-"], input=ASSEMBLY,
                           text=True, check=True)
            cls.calls = wire_calls(obj, objdump)
            cls.events = event_calls(obj, objdump)

    def test_callee_saved_property_survives_call_and_add(self):
        self.assertEqual(self.calls["saved_property"], [
            {"op": "read", "property": 0x2000c, "size": 4}])

    def test_caller_saved_property_is_unknown_after_call(self):
        self.assertEqual(self.calls["clobbered_property"], [
            {"op": "read", "property": None, "size": 4}])

    def test_conflicting_branch_properties_are_not_evidence(self):
        self.assertEqual(self.calls["conflicting_paths"], [
            {"op": "read", "property": None, "size": 4}])

    def test_second_pair_load_destination_is_not_stale(self):
        self.assertEqual(self.calls["overwritten_pair"], [
            {"op": "read", "property": None, "size": 4}])

    def test_w_register_argument_discards_upper_bits(self):
        self.assertEqual(self.calls["narrowed_property"], [
            {"op": "write", "property": 0x2000c, "size": 4}])

    def test_event_code_is_separate_from_type_and_payload(self):
        self.assertEqual(self.events["event_sender"], [{"type": 5, "event": 0x2a}])


if __name__ == "__main__":
    unittest.main()
