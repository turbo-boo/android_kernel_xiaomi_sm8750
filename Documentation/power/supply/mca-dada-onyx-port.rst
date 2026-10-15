Dada MCA port from Onyx
======================

The Dada tree now contains the Onyx wired buck/PD/PPS/QC state machines,
wireless basic/quick/reverse controllers, full fuel-gauge strategy with SOC
smoothing and shutdown policy, smart-charge scene/night/SOC/bypass policy,
thermal policy and battery/USB/wireless power-supply implementations.

The hardware port covers the ICs declared by Dada's DT: BQ27Z561, SC8585
(implemented by sc8581.c), NU1652 and HL7603. The temporary compatibility
voters and BAA-only/read-only/no-charging strategy replacements are removed.
Stock module filenames and the Dada Kleaf path translations remain intact.

References
----------

* Onyx: xiaomi-onyx-playground/android_kernel_xiaomi_sm8735,
  lineage-23.2, commit 737c220c601ec2202ada953041770aa4c6df7e66.
* Dada DT: MiCode/kernel_devicetree, dada-v-oss,
  commit 233bd52eddce9f081f8e87802796f5994babafa5,
  qcom/mca.dtsi and qcom/dada-charger-common.dtsi.
* Stock dump: https://dumps.tadiphone.dev/dumps/xiaomi/dada.
  The Android 15 CN OS2.0.101.0 branch resolves to
  1125d12771fff547cad324aa7cd3961cd9156489. All 50 stock MCA modules from
  vendor_boot/ramdisk/lib/modules were downloaded and checked against their
  Git blob IDs. Their names, 419 exports, OF aliases, firmware hashes and
  selected wire calls are recorded in dada_stock_contract.json. The dump's
  vermagic is 6.6.30-android15-8-g48bbe4889b2a-dirty-4k with modversions.

Dada adaptations
----------------

Dada's GLINK owners/opcodes, hardware ID handling, path-control/connector/
OVP/OCP/BMD/LPD monitors, shared-memory/partition access and DT-driven
single-pack JEITA policy are retained. Stock disassembly corrects the sub-PMIC
property IDs shifted by Onyx-only enum entries, PD IDs starting at CC toggle,
the legacy PPS power-budget write and the wireless input-current limit
(0x1003). USB type polling reads ``USB_REAL_TYPE`` (0x2000c), as does the
getter. Stock computes this as ``w20 = 0x20001; w0 = w20 + 0xb``; the
separate ``w2 = 4`` is the response size, not the property ID. The earlier
property-4 fixture and polling change were incorrect and are removed.
The stock OTG getters and pen notifier exports are restored in their original
provider modules.

Stock BTF also fixes the shared event IDs (0x00 through 0x38). Onyx-only
features use a separate port-local range from 0x100 and cannot shift Dada's
IDs. The sub-PMIC callbacks emit charge-abnormal/restore events 42/43 and
Type-C-port event 53. Boost notifications always drive the CP OVP gate with
``enable = (value == 0)``, independently of the configured boost source.
QC voltage/command setters propagate GLINK errors, and charge-done reads
return negative transport errors without changing the caller's output.
Sink-power-suspend notifications read a single byte; unsupported notification
IDs 9 through 11 are ignored as in stock.

Client vote/enabled queries reserve a client slot as in stock, preserving the
32-client limit and registration-order tie breaking. An untouched OR client
returns zero, rather than ``-EINVAL`` being interpreted as input suspension.
PD verification clears on charger-connect notifications with null payloads
and on charge-abnormal events. Fixed-PD voltage writes are not blocked during
verification. PD notifier registration failures unwind all acquired callbacks
and providers before devm state can be released.

GLINK accepts non-null payload buffers with lengths 0 through 255. In
particular, the connector anti-burn ``RESET_VSAFE0V`` command (0xff) reaches
ADSP as a zero-length VDM_NAK (0x2100b) write instead of failing in either the
PD provider or the shared transport.

The VBAT OVP event publishes a 32-bit status, matching the business charger's
four-byte payload copy rather than exposing a one-byte bool and adjacent
memory. Connector revision gates and boost-source overrides use HWID
``build_version``, not ``major_version``. VBAT OVP, path-control, BMD and LPD
diagnostics join the connector under ``xm_power/hw_monitor``, as in stock.
BMD exports ``btb_missing_status`` and reads the master/slave GPIO or FG
channels live instead of returning the last monitor sample.

Path-control attributes return the raw bit masks 1/2/4/8/16. The writable
``control_value`` applies a one-shot route override without replacing the
normal source-state mask. Parsed, recognized sysfs writes return their byte
count even when no route applies; internal gate APIs retain their error
reporting and validation.

Controls not established by this Dada dump (PMIC buck FSW, restart AICL,
pack current/temperature, AICL status, too-hot-limit and PD gear shift) are
left unregistered in the Dada ADSP providers instead of using guessed IDs.
The class interfaces remain available to other providers. The folding-device
``flip_charge_curr`` voter is optional when base/flip support is disabled.
Termination current uses the restrictive MIN election required by Dada JEITA.

BASP retains Dada's 36-byte header and inline 24-byte curve header. Each open
file owns its own page; closing takes an immutable snapshot before validation.
The parser validates checksum, complete size, all counts/indices/ranges,
duplicate rows, FFC section identity and voltage/current step semantics before
dispatch. Wired/wireless curve updates stage allocations and reject updates
while that quick-charge controller is online. Temperature bands remain DT-owned.
A consumer error is returned; application across independent controllers is
not a rollback transaction.

Callback registries use SRCU, which permits sleeping/nested class callbacks.
Providers detach before their state is released. Stop flags, notifier/thread
shutdown, work draining and diagnostic removal replace empty Onyx remove paths.
Voter addresses are retained in the core registry because consumers cache them;
removal detaches the callback and data, and a later bind can reuse the entry.
Force-stop paths do not synchronously cancel their currently executing monitor.
Stack-owned work callback scopes track the task inside MCA, avoiding an import
of ``current_work`` which is absent from the public GKI KMI symbol lists.

Declarations shared with prebuilt vendor modules keep the stock spelling,
because genksyms hashes typedef names, attributes and enumerators. The hwid
getters and the ``struct mca_hwid`` fields are ``uint32_t``, with
``build_adc`` before ``product_adc``. The MCA log functions carry no format
attribute, and ``mca_sysfs_create_files()`` takes a ``const int`` size.
``qti_battery_charger_get_prop()`` takes ``enum battery_charger_prop``, which
includes ``FLASH_ACTIVE``. The panel notifier client enum includes the thermal,
second thermal and keyboard clients. The resulting symbol versions match the
stock Dada modules.

Firmware and external dependencies
----------------------------------

NU1652 firmware download/erase/bin controls and vendor/index selection are
ported, including index bounds checks and serialized, bounded binary staging.
A private download copy preserves staged bytes on retry. Stock fw_data_1652
contains two 32768-byte images; neither equals the included Onyx image
(SHA-256 6a117bd93850ba93a80d1241eb23891f2ece6f5cc346d73e6d30e928048b2557).
The stock image hashes are recorded in dada_stock_contract.json. Destructive
firmware operations therefore return ``-EOPNOTSUPP`` unless the board explicitly supplies
``xiaomi,allow-firmware-update``. The official Dada DT does not opt in.
Firmware status still reads the installed version, but does not advertise
an upgrade against the unapproved Onyx image while that option is disabled.
No device firmware is written by the validation workflow.

The MIEV reporting implementation follows Onyx and requires its Xiaomi MIEV
transport when CONFIG_MIEV is enabled. A build with that transport disabled
does not establish telemetry delivery. ADSP firmware and the Android charging
HAL are external runtime dependencies.

Public Kleaf workspace
----------------------

``tools/mca/setup_dada_kleaf.py`` assembles a separate workspace from the
committed source checkout and 41 public MiCode/AOSP projects. Every external
commit is pinned in ``tools/mca/dada_kleaf_sources.json``. It uses MiCode's
Onyx ``kernel_build`` release, the official Dada DT/display/camera/touch/WLAN
releases and AOSP ``common`` tag ``android15-6.6-2024-07_r59``. The GKI has
Linux 6.6.30, KMI generation 8, Clang r510928 and Rust 1.73.0b, matching the
Dada build constants. Its modular KUnit configuration matches Dada; the
initial July r1 tag instead builds KUnit into vmlinux and cannot be combined
with the unchanged Dada KUnit module configuration.

The legacy ``include/linux/mca`` symlink points to the restored in-tree
headers. This also fixes the existing charger-ulog GLINK and DWC3 consumers,
which otherwise reference the omitted external proprietary MCA directory.

On Linux x86_64 with Git, Python 3.9 or newer and kernel host dependencies,
start from a committed checkout and run::

  python3 tools/mca/setup_dada_kleaf.py \
    --workspace ../dada-kernel-workspace --build

The script refuses conflicting existing revisions instead of resetting them.
``--verify-only`` checks an already populated workspace. The build uses the
unmodified Dada perf configuration, normal ``FACTORY_BUILD=0``, a bounded
job count and local kernel caching. Auxiliary actions retain Kleaf's standard
isolation; forcing all actions to run locally can reuse stale extraction
symlinks and break repeated archive extraction. It does not relax modpost
errors or update KMI symbol allowlists.

Off-device validation
---------------------

The public Kleaf ``//msm-kernel:dada_perf`` target builds successfully with
the pinned workspace. Both GKI and vendor modules enable CFI and modversions.
All 52 packaged MCA modules, eight base DTBs and the Dada overlay DTBO are
generated, and the Kleaf KMI symbol-list violation check passes. The compiled
MCA modules pass the same packaging, alias, export and wire-contract checks
as the generic build. All 199 recorded stock core import CRCs match the
resulting GKI. No modpost error relaxation or Dada config exclusions are used.

``//msm-kernel:dada_perf_abi`` also builds successfully, including the full
vendor vmlinux, and generates ``abi-full.stg`` and ``abi.stg``. The public
Dada rule supplies no ``abi_definition_stg`` baseline. This verifies ABI
extraction, rather than comparison against a published Dada vendor type
baseline.

The MCA workflow builds the selected arm64 kernel Image and modules without
relaxing modpost errors, in addition to compiling the MCA/wireless subtrees.
The verification configuration enables MHI_BUS_MISC, IPC_LOGGING, the vendor UFS
PHY, QCOM_IOMMU_UTIL, QTI_PMIC_GLINK and Android vendor hooks/OEM data. The
PMIC-GLINK selection compiles the real transport rather than its header's
``-ENODEV`` stubs. It excludes the
obsolete Q6V5_MSS remoteproc driver
(which has duplicate reg_info definitions in this public tree) and the
legacy Qualcomm interconnect drivers (their upstream node layout conflicts
with the vendor-modified interconnect headers), CoreSight USB, Freescale
ENETC and Mellanox MLX5 (unrelated diagnostics exceed fortified netlink buffers).
These exclusions do not remove MCA or its transport dependencies. This
generic verification configuration is not a device boot image.

JEITA tests cover 68 checks against the official CN/Global tables. BASP tests
cover 20,207 checks with address/undefined-behavior sanitizers, including
unaligned input, truncation, duplicate/invalid rows and deterministic malformed
packets. verify_dada_port.py checks the 52 packaged MCA module paths and the
recorded official Dada DT bindings, then checks the compiled OF aliases and
presence of the full kernel Module.symvers and an acyclic MCA dependency graph.
verify_stock_contract.py checks all 50 stock module names, 419 public exports,
stock aliases, 90 selected ADSP wire calls and six constant event type/code
pairs in the compiled modules against independently recorded stock evidence.
Unknown/dynamic VDM arguments and branch-merged event arguments are not
claimed as constant call evidence. The optional ``--gki-symvers`` check
compares 199 core import CRCs extracted
from the stock modules with GKI ``Module.symvers``. The fixture records the
stock provenance and the matching public GKI used to select those core
imports. ELF section symbols such as ``__ksymtab_strings`` and
``__ksymtab_gpl`` are excluded from the export count. This comparison does
not establish every vendor/MCA ABI, structure layout or runtime behavior.

The wire decoder follows integer constants through callee-saved registers,
register moves and immediate arithmetic. Calls discard caller-saved values;
local branch joins discard constants that differ between reachable paths.
``wire_contract_test.py`` compiles real AArch64 objects to cover these cases,
pair-load overwrites, 32-bit argument narrowing and the distinction between
event type (x0), event code (x1) and payload (x2), and runs in MCA CI.

Revalidate the evidence fixture itself against the pinned stock binaries with::

  python3 tools/testing/selftests/mca/verify_stock_contract.py \
    --stock-modules /path/to/vendor_boot/ramdisk/lib/modules \
    --objdump aarch64-linux-gnu-objdump

This checks all 50 module SHA-256 hashes before trusting their contracts and
uses the recorded stock function names for the 90 wire and six event checks. It can be
combined with ``--kernel-out`` to check both the evidence and the rebuilt
modules. A compiled-module comparison alone cannot detect a fixture copied
from an incorrect implementation.

An off-device instruction-execution comparison ran the stock sub-PMIC ELF and
functions rebuilt from the corrected source under AArch64 emulation. The 29
scenarios cover negative/zero/positive GLINK results, unchanged charge-done
outputs on failure, charge-status/Type-C/capability events, CP gate polarity
for both boost-source configurations, PPS dispatch, ignored notification IDs
and one-byte sink payloads at a mapped-page boundary. The pre-fix source
reproduced swallowed transport errors, wrong Type-C dispatch, missing CP gate
operations and a boundary-crossing sink read; the corrected instructions
match stock in these scenarios. Additional instruction runs compared 64 voter
result/capacity/allocation/tie-order checks, 13 GLINK length/error checks and
six PD/GLINK command/verification transitions against stock. Four PD probe
failure cases also left no live callbacks or providers. Kernel services and
transport replies were controlled by the harness, not real ADSP firmware or
charging hardware.

Monitor instruction comparisons also covered 192 path-mask readbacks, 24
accepted writes with no route and four one-shot overrides that preserve the
source mask while driving the CP gate on/off without duplicate operations.
Eighteen BMD reads check live FG/GPIO values and cached missing status; six
connector revision gates and six boost-source overrides vary major/build
versions independently. Four VBAT samples check trigger/clear notification
payloads, no-repeat behavior and scheduling, including poisoned bytes next to
the old bool payload. Pre-fix instruction runs reproduce payload contamination,
stale BTB reads, incorrect HWID gates/boost overrides and path read/write
differences; the corrected instructions match stock for these cases.

All these behavioral comparisons also pass using instructions from the final
linked modules, with source-probe metadata supplying only field offsets.
Executing the compiled sysfs call setup checks ``hw_monitor`` and the link
names at create/remove sites in all five stock-backed monitors; it does not
exercise complete monitor probes or prove physical device registration.

The optional QEMU smoke test boots the generic arm64 Image on ``virt`` and
loads all 52 MCA modules with their dependency closure. It checks two complete
load/unload cycles, removal of the ``xm_power`` class and an empty module list.
It exercises module initialization, registration and cleanup without MCA DT
nodes, I2C chips or ADSP services; driver probe/remove paths are not covered.
It requires QEMU and a static AArch64 BusyBox binary::

  python3 tools/testing/selftests/mca/qemu_module_smoke.py \
    --kernel-out /path/to/kernel-out --busybox /path/to/arm64-busybox \
    --log /path/to/mca-qemu.log

The Kleaf-built GKI Image and Dada modules also pass two MCA load/unload
cycles with CFI and modversions enabled: 52 MCA modules and 22 platform
dependencies, with no warning/oops and no residual MCA modules or ``xm_power``
class. For this configuration, use ``--keep-dependencies``: Qualcomm platform
modules such as ``debug_symbol`` have no unload path, and others retain
platform resources. Dependencies are loaded once before the cycles; after
each cycle the module list must match that baseline exactly. The generic
smoke retains its stricter complete-dependency-unload and empty-list checks.
Neither mode probes physical MCA hardware. The separate GKI Image can be
supplied with ``--image`` while ``--kernel-out`` names the complete vendor
make output directory containing ``modules.order``. For example::

  python3 tools/testing/selftests/mca/qemu_module_smoke.py \
    --kernel-out /path/to/vendor-out --image /path/to/GKI/Image \
    --keep-dependencies --busybox /path/to/arm64-busybox \
    --log /path/to/mca-dada-qemu.log

Device probing, charging/thermal/SOC accuracy, reconnect behavior, suspend and
shutdown, wireless power/FOD/reverse behavior and firmware compatibility still
require hardware validation. These are not inferred from successful builds.
