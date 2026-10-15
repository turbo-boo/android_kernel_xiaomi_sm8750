// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/module.h>
#include <linux/soc/qcom/smem.h>
#include <linux/types.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_smem.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_smem"
#endif

#define SMEM_ID_VENDOR_BATTERY_INFO 0x51

struct mca_smem_battery_info {
	u32 reserved[8];
	u32 zero_speed_start_mode;
	u8 verify_result;
	u8 cell_sn;
};

static struct mca_smem_battery_info *mca_smem_get_battery_info(void)
{
	struct mca_smem_battery_info *info;
	int ret;

	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR_BATTERY_INFO,
			      sizeof(*info));
	if (ret < 0 && ret != -EEXIST) {
		mca_log_err("unable to allocate SMEM battery entry: %d\n", ret);
		return ERR_PTR(ret);
	}

	info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_ID_VENDOR_BATTERY_INFO,
			     NULL);
	if (IS_ERR(info))
		mca_log_err("unable to acquire SMEM battery entry: %ld\n",
			    PTR_ERR(info));
	return info;
}

int get_smem_battery_info(int *is_zero_speed)
{
	struct mca_smem_battery_info *info;

	if (!is_zero_speed)
		return -EINVAL;
	info = mca_smem_get_battery_info();
	if (IS_ERR(info))
		return PTR_ERR(info);
	*is_zero_speed = info->zero_speed_start_mode;
	return 0;
}
EXPORT_SYMBOL(get_smem_battery_info);

int get_smem_battery_verify_result(u8 *verified, u8 *chip_ok)
{
	struct mca_smem_battery_info *info;

	if (!verified || !chip_ok)
		return -EINVAL;
	info = mca_smem_get_battery_info();
	if (IS_ERR(info))
		return PTR_ERR(info);
	*verified = (info->verify_result >> 5) & 1;
	*chip_ok = (info->verify_result >> 4) & 1;
	return 0;
}
EXPORT_SYMBOL(get_smem_battery_verify_result);

int get_smem_battery_cell_sn(u32 *cell_sn)
{
	struct mca_smem_battery_info *info;

	if (!cell_sn)
		return -EINVAL;
	info = mca_smem_get_battery_info();
	if (IS_ERR(info))
		return PTR_ERR(info);
	*cell_sn = info->cell_sn;
	return 0;
}
EXPORT_SYMBOL(get_smem_battery_cell_sn);

MODULE_DESCRIPTION("Xiaomi MCA Qualcomm SMEM battery state reader");
MODULE_LICENSE("GPL v2");
