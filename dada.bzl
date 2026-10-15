load(":target_variants.bzl", "la_variants")
load(":msm_kernel_la.bzl", "define_msm_la")
load(":image_opts.bzl", "boot_image_opts")
load(":xiaomi_sm8750_common.bzl", "xiaomi_common_in_tree_modules")
load(":sun.bzl", 
        "target_arch",
        "target_arch_in_tree_modules",
        "target_arch_consolidate_in_tree_modules",
        "consolidate_board_kernel_cmdline_extras",
        "consolidate_board_bootconfig_extras",
        "consolidate_kernel_vendor_cmdline_extras",
        "perf_board_kernel_cmdline_extras",
        "perf_board_bootconfig_extras",
        "perf_kernel_vendor_cmdline_extras",
)

target_name = "dada"

# The public Sun module list still names several proprietary MCA paths that do
# not exist in the reconstructed in-tree Dada MCA layout. Translate only those
# inherited entries here so other Sun-family targets keep their original ABI.
_dada_mca_module_replacements = {
    "drivers/power/supply/mca/mca_hardware_ic/subpmic/qcom_subpmic/mca_qcom_subpmic_proxy.ko": "drivers/power/supply/mca/mca_qcom_subpmic_proxy/mca_qcom_subpmic_proxy.ko",
    "drivers/power/supply/mca/mca_platform_sysfs/mca_qcom_sysfs.ko": "drivers/power/supply/mca/mca_qcom_sysfs/mca_qcom_sysfs.ko",
    "drivers/power/supply/mca/mca_strategy/strategy_charger/mca_buckchg_jeita.ko": "drivers/power/supply/mca/mca_strategy/strategy_jeita/mca_buckchg_jeita.ko",
    "drivers/power/supply/mca/mca_strategy/strategy_charger/mca_charger_thermal.ko": "drivers/power/supply/mca/mca_strategy/strategy_thermal/mca_charger_thermal.ko",
    "drivers/power/supply/mca/mca_strategy/strategy_charger/mca_strategy_buckchg.ko": "drivers/power/supply/mca/mca_strategy/strategy_buckchg/mca_strategy_buckchg.ko",
    "drivers/power/supply/mca/mca_strategy/strategy_charger/mca_strategy_quickchg.ko": "drivers/power/supply/mca/mca_strategy/strategy_quickchg/mca_strategy_quickchg.ko",
    "drivers/power/supply/mca/mca_strategy/strategy_fg/mca_soc_limit.ko": "drivers/power/supply/mca/mca_strategy/strategy_soc_limit/mca_soc_limit.ko",
    "drivers/power/supply/mca/mca_strategy/strategy_pd/mca_pd_auth.ko": "drivers/power/supply/mca/mca_strategy/strategy_pd_auth/mca_pd_auth.ko",
}

# Modules added by the reconstruction that have no legacy Sun list entry.
_dada_mca_extra_modules = [
    "drivers/power/supply/mca/mca_hardware_monitor/mca_ibat_ocp_monitor.ko",
    "drivers/power/supply/mca/mca_platform/mca_platform_loadsw_class.ko",
]

def define_dada():
    for variant in la_variants:
        _sun_modules_for_dada = [
            _dada_mca_module_replacements.get(module, module)
            for module in target_arch_in_tree_modules
        ]
        _target_in_tree_modules = _sun_modules_for_dada + _dada_mca_extra_modules + xiaomi_common_in_tree_modules + [
            # keep sorted
            "drivers/media/rc/ir-spi.ko",
            # MIUI ADD: Stability_DebugEnhance
            "drivers/xiaomi/boottime/boottime.ko",
            # END Stability_DebugEnhance
            "drivers/xiaomi/mi_stack/mi_stack.ko",
            "drivers/xiaomi/mi_ubt/mi_ubt.ko",
            "drivers/xiaomi/mi_ubt/test/mi_ubt_test.ko",
            "drivers/xiaomi/printk_enhance/printk_enhance.ko",
            "fs/nls/nls_ucs2_utils.ko",
            "fs/netfs/netfs.ko",
            "net/dns_resolver/dns_resolver.ko",
            "fs/smb/common/cifs_md4.ko",
            "fs/smb/common/cifs_arc4.ko",
            "fs/smb/client/cifs.ko",
            "drivers/xiaomi/bootmonitor/bootmonitor.ko",
            "drivers/mtd/mtd_blkdevs.ko",
            "drivers/mtd/parsers/ofpart.ko",
            "drivers/mtd/mtdoops.ko",
            "drivers/mtd/devices/block2mtd.ko",
            "drivers/mtd/chips/chipreg.ko",
            "drivers/mtd/mtdblock.ko",
            "drivers/mtd/mtd.ko",
            "drivers/sandbox/rt_mod.ko",

            "drivers/mihw/powersave/powersave.ko",

            "drivers/block/zram/zram.ko",
            "mm/zsmalloc.ko",
            "drivers/xiaomi/mi_trace/mi_trace.ko",
            ]

        _target_consolidate_in_tree_modules = _target_in_tree_modules + \
                target_arch_consolidate_in_tree_modules + [
            # keep sorted
            "drivers/dma-buf/dma-buf-ref.ko",
            ]

        if variant == "consolidate":
            mod_list = _target_consolidate_in_tree_modules
            board_kernel_cmdline_extras = consolidate_board_kernel_cmdline_extras
            board_bootconfig_extras = consolidate_board_bootconfig_extras
            kernel_vendor_cmdline_extras = consolidate_kernel_vendor_cmdline_extras
        else:
            mod_list = _target_in_tree_modules
            board_kernel_cmdline_extras = perf_board_kernel_cmdline_extras
            board_bootconfig_extras = perf_board_bootconfig_extras
            kernel_vendor_cmdline_extras = perf_kernel_vendor_cmdline_extras

        define_msm_la(
            msm_target = target_name,
            msm_arch = target_arch,
            variant = variant,
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                earlycon_addr = "qcom_geni,0x00a9c000",
                kernel_vendor_cmdline_extras = kernel_vendor_cmdline_extras,
                board_kernel_cmdline_extras = board_kernel_cmdline_extras,
                board_bootconfig_extras = board_bootconfig_extras,
            ),
        )
