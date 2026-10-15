.. SPDX-License-Identifier: GPL-2.0

================
Dada MCA JEITA
================

The single-pack driver replaces the earlier BAA-only consumer with a DT-based
buck charging policy. It is adapted from ``mca_buckchg_jeita.c`` and its header
in ``xiaomi-onyx-playground/android_kernel_xiaomi_sm8735`` at commit
``737c220c601ec2202ada953041770aa4c6df7e66``. Onyx current/voltage tables are not
copied: all battery limits come from the device's DT and accepted BAA overrides.

DT interface
============

``jeita_para`` is a string array with eight columns per temperature row::

    temp_low temp_high low_temp_hys high_temp_hys max_current vterm iterm volt_para_name

Temperature bounds and hysteresis use Celsius. Gauge readings use deci-Celsius;
range comparisons retain that precision, including temperatures just below zero.
``vterm`` and voltage thresholds use mV, and currents use mA. A voltage table
contains pairs of voltage threshold and maximum current; ``null`` selects the
row's fixed current. Tables are bounded to 15 temperature rows and four voltage
steps per row, matching the public source. Temperature rows cannot overlap and
voltage thresholds must strictly increase.
The official Dada tables use ``-65535`` and ``65535`` as the outer temperature
bounds; these sentinel values are supported without narrowing the DT ranges.

``jeita_para_ffc`` is optional. Normal policy remains available if an FFC table
is absent. The source's ``has-global-batt-para``, ``has-tmp-batt-para``,
``vbat_low_hyst``, ``vbat_low_cold_hyst``, ``jeita_hot_termination_hyst`` and
``base-flip-same`` properties are supported. Foldable ``support-base-flip`` is
rejected because this implementation has one gauge/pack.

Policy and lifetime
===================

The monitor discovers already-connected sources, handles USB/wireless events,
and rechecks every three seconds, retrying failures after one second. Adjacent
temperature bands use hysteresis; zero-current bands, out-of-table temperatures
and large temperature jumps do not retain a stale permissive band. Voltage
steps reduce current immediately and use the configured hysteresis before
increasing it. State belongs to each device, not function-static variables.

``chg_enable`` (AND), ``buck_charge_curr``, ``term_volt`` and ``term_curr`` (MIN)
feed the existing Dada buck controls. The exported policy bridge pins the buck
module and serializes its voter lifetime. Existing userspace disable/current
votes remain part of the election, and termination sysfs writes now use the
same policy bridge. Failed ADSP writes propagate back to the policy and are
retried. Initial programming and error recovery retain a stop vote until limits
are applied; normal polling does not toggle charging off/on.

BAA modifies termination parameters and the final voltage threshold of existing
DT rows by index. Normal and FFC overrides are validated on a private copy and
published together. Invalid indices, duplicates, negative termination current,
nonpositive termination voltage and reordered voltage steps are rejected without
changing the live tables. The smart-charge layer propagates consumer failures.
Removal unregisters callbacks and drains work before releasing device state;
the strategy registry uses SRCU to wait for in-flight callbacks.

Validation and remaining work
=============================

``tools/testing/selftests/mca/jeita_policy_test.c`` exercises temperature edges,
hysteresis, hard stops, voltage-step changes, malformed tables and termination
overrides. It also validates the actual CN/Global normal/FFC JEITA tables from
``MiCode/kernel_devicetree`` commit
``233bd52eddce9f081f8e87802796f5994babafa5`` in
``qcom/dada-charger-common.dtsi``. Those tables are fixtures for tests only.
The MCA compile workflow runs this test with address/undefined-behavior
sanitizers before compiling the arm64 module tree.

Compilation with ``modules_prepare`` checks source/Kbuild/module composition.
Without the full kernel's ``Module.symvers``, modpost warnings do not establish
complete kernel dependency correctness. The Dada DT tables, GLINK controls,
thermal interaction, reconnect behavior and module binding still require device
validation. Wired/wireless high-power CP state machines and reverse wireless
are now provided by the Onyx port; see ``mca-dada-onyx-port.rst`` for scope,
firmware gating and the separate full-kernel validation workflow.
