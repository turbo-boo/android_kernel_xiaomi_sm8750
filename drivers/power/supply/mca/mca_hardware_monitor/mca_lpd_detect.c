// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA USB liquid/debris detection userspace bridge for Dada. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_buckchg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_lpd_detect"
#endif

struct dada_lpd {
	struct device *dev;
	int lpd_charging;
};

enum dada_lpd_attr {
	LPD_PROP_EN = 0,
	LPD_PROP_STATUS,
	LPD_PROP_SBU1,
	LPD_PROP_SBU2,
	LPD_PROP_CC1,
	LPD_PROP_CC2,
	LPD_PROP_DP,
	LPD_PROP_DM,
	LPD_PROP_CHARGING,
	LPD_PROP_CONTROL,
	LPD_PROP_UART_CONTROL,
};

static struct dada_lpd *g_lpd;

int lpd_is_charging_limit(void)
{
	return g_lpd ? g_lpd->lpd_charging : 0;
}
EXPORT_SYMBOL(lpd_is_charging_limit);

static ssize_t dada_lpd_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct dada_lpd *lpd = dev_get_drvdata(dev);
	int value = 0, ret = 0;

	if (!lpd)
		return -ENODEV;

	if (!strcmp(attr->attr.name, "enable"))
		ret = platform_class_buckchg_ops_get_lpd_enable(MAIN_BUCK_CHARGER,
							       &value);
	else if (!strcmp(attr->attr.name, "lpd_status"))
		ret = platform_class_buckchg_ops_get_lpd_status(MAIN_BUCK_CHARGER,
							       &value);
	else if (!strcmp(attr->attr.name, "sbu1"))
		ret = platform_class_buckchg_ops_get_lpd_sbu1(MAIN_BUCK_CHARGER,
							     &value);
	else if (!strcmp(attr->attr.name, "sbu2"))
		ret = platform_class_buckchg_ops_get_lpd_sbu2(MAIN_BUCK_CHARGER,
							     &value);
	else if (!strcmp(attr->attr.name, "cc1"))
		ret = platform_class_buckchg_ops_get_lpd_cc1(MAIN_BUCK_CHARGER,
							    &value);
	else if (!strcmp(attr->attr.name, "cc2"))
		ret = platform_class_buckchg_ops_get_lpd_cc2(MAIN_BUCK_CHARGER,
							    &value);
	else if (!strcmp(attr->attr.name, "dp"))
		ret = platform_class_buckchg_ops_get_lpd_dp(MAIN_BUCK_CHARGER,
							   &value);
	else if (!strcmp(attr->attr.name, "dm"))
		ret = platform_class_buckchg_ops_get_lpd_dm(MAIN_BUCK_CHARGER,
							   &value);
	else if (!strcmp(attr->attr.name, "lpd_charging"))
		return sysfs_emit(buf, "%d\n", lpd->lpd_charging);
	else if (!strcmp(attr->attr.name, "lpd_control"))
		ret = platform_class_buckchg_ops_get_lpd_control(MAIN_BUCK_CHARGER,
								&value);
	else if (!strcmp(attr->attr.name, "uart_control"))
		ret = platform_class_buckchg_ops_get_lpd_uart_control(
			MAIN_BUCK_CHARGER, &value);
	else
		return -EINVAL;

	return ret ? ret : sysfs_emit(buf, "%d\n", value);
}

static ssize_t dada_lpd_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct dada_lpd *lpd = dev_get_drvdata(dev);
	int value, ret = 0;

	if (!lpd)
		return -ENODEV;
	if (kstrtoint(buf, 10, &value))
		return -EINVAL;

	if (!strcmp(attr->attr.name, "sbu1"))
		ret = platform_class_buckchg_ops_set_lpd_sbu1(MAIN_BUCK_CHARGER,
							     value);
	else if (!strcmp(attr->attr.name, "lpd_charging"))
		lpd->lpd_charging = value;
	else if (!strcmp(attr->attr.name, "lpd_control"))
		ret = platform_class_buckchg_ops_set_lpd_control(MAIN_BUCK_CHARGER,
								value);
	else if (!strcmp(attr->attr.name, "uart_control"))
		ret = platform_class_buckchg_ops_set_lpd_uart_control(
			MAIN_BUCK_CHARGER, value);
	else
		return -EACCES;

	return ret ? ret : count;
}

static struct mca_sysfs_attr_info dada_lpd_attrs_info[] = {
	mca_sysfs_attr_ro(dada_lpd, 0440, LPD_PROP_EN, enable),
	mca_sysfs_attr_ro(dada_lpd, 0440, LPD_PROP_STATUS, lpd_status),
	mca_sysfs_attr_rw(dada_lpd, 0664, LPD_PROP_SBU1, sbu1),
	mca_sysfs_attr_ro(dada_lpd, 0440, LPD_PROP_SBU2, sbu2),
	mca_sysfs_attr_ro(dada_lpd, 0440, LPD_PROP_CC1, cc1),
	mca_sysfs_attr_ro(dada_lpd, 0440, LPD_PROP_CC2, cc2),
	mca_sysfs_attr_ro(dada_lpd, 0440, LPD_PROP_DP, dp),
	mca_sysfs_attr_ro(dada_lpd, 0440, LPD_PROP_DM, dm),
	mca_sysfs_attr_rw(dada_lpd, 0664, LPD_PROP_CHARGING, lpd_charging),
	mca_sysfs_attr_rw(dada_lpd, 0664, LPD_PROP_CONTROL, lpd_control),
	mca_sysfs_attr_rw(dada_lpd, 0664, LPD_PROP_UART_CONTROL, uart_control),
};
static struct attribute *dada_lpd_attrs[ARRAY_SIZE(dada_lpd_attrs_info) + 1];
static const struct attribute_group dada_lpd_group = { .attrs = dada_lpd_attrs };

static int dada_lpd_probe(struct platform_device *pdev)
{
	struct dada_lpd *lpd;
	int ret;

	lpd = devm_kzalloc(&pdev->dev, sizeof(*lpd), GFP_KERNEL);
	if (!lpd)
		return -ENOMEM;
	lpd->dev = &pdev->dev;
	platform_set_drvdata(pdev, lpd);
	dev_set_drvdata(&pdev->dev, lpd);
	g_lpd = lpd;

	mca_sysfs_init_attrs(dada_lpd_attrs, dada_lpd_attrs_info,
			     ARRAY_SIZE(dada_lpd_attrs_info));
	ret = mca_sysfs_create_link_group(SYSFS_DEV_5, "lpd", &pdev->dev,
					  &dada_lpd_group);
	if (ret) {
		g_lpd = NULL;
		return dev_err_probe(&pdev->dev, ret,
				     "failed to create LPD sysfs\n");
	}
	mca_log_info("LPD userspace bridge ready\n");
	return 0;
}

static int dada_lpd_remove(struct platform_device *pdev)
{
	struct dada_lpd *lpd = platform_get_drvdata(pdev);

	mca_sysfs_remove_link_group(SYSFS_DEV_5, "lpd", &pdev->dev,
				    &dada_lpd_group);
	if (g_lpd == lpd)
		g_lpd = NULL;
	return 0;
}

static const struct of_device_id dada_lpd_match[] = {
	{ .compatible = "mca,lpd_detect" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_lpd_match);

static struct platform_driver dada_lpd_driver = {
	.driver = {
		.name = "mca_lpd_detect",
		.of_match_table = dada_lpd_match,
	},
	.probe = dada_lpd_probe,
	.remove = dada_lpd_remove,
};
module_platform_driver(dada_lpd_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA liquid/debris detection bridge");
MODULE_LICENSE("GPL v2");
