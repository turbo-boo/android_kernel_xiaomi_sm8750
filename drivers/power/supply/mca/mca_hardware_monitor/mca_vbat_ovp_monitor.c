// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA software VBAT OVP monitor for Dada. */
#include <mca/common/mca_workqueue.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include "hwid.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_vbat_ovp_mon"
#endif

#define VBAT_OVP_THR_FFC_DEFAULT_MV       4580
#define VBAT_OVP_THR_NORMAL_DEFAULT_MV    4530
#define VBAT_OVP_THR_FFC_GL_DEFAULT_MV    4530
#define VBAT_OVP_THR_NORMAL_GL_DEFAULT_MV 4480
#define VBAT_OVP_HYS_DEFAULT_MV           15
#define VBAT_OVP_RECHARGE_DEFAULT_MV      50
#define VBAT_OVP_FAST_MS                   10000
#define VBAT_OVP_NORMAL_MS                 60000
#define VBAT_OVP_TEMP_SKIP_DECIC           480

enum dada_fg_topology {
	DADA_FG_SINGLE = 0,
	DADA_FG_PARALLEL,
	DADA_FG_SERIES,
	DADA_FG_SINGLE_SERIES,
};

enum dada_vbat_attr {
	DADA_VBAT_FAKE_DEBUG = 0,
	DADA_VBAT_FAKE_OVERRIDE,
};

struct dada_vbat_ovp {
	struct device *dev;
	struct delayed_work work;
	int threshold_ffc_mv;
	int threshold_normal_mv;
	int threshold_ffc_global_mv;
	int threshold_normal_global_mv;
	int active_ffc_mv;
	int active_normal_mv;
	int hysteresis_mv;
	int recharge_delta_mv;
	int fg_type;
	int fake_debug_mv;
	int fake_override_mv;
	bool support_global_fv;
	bool triggered;
};

static int dada_vbat_power_present(bool *present)
{
	int usb = 0, wls = 0;
	int ret_usb, ret_wls;

	if (!present)
		return -EINVAL;
	ret_usb = platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &usb);
	ret_wls = platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &wls);

	/* During partial bring-up either transport may legitimately be absent. */
	if (ret_usb == -EOPNOTSUPP || ret_usb == -ENODEV)
		usb = 0;
	else if (ret_usb)
		return ret_usb;
	if (ret_wls == -EOPNOTSUPP || ret_wls == -ENODEV)
		wls = 0;
	else if (ret_wls)
		return ret_wls;

	*present = !!usb || !!wls;
	return 0;
}

static int dada_vbat_ovp_sample(struct dada_vbat_ovp *info, bool *ovp)
{
	bool present = false;
	int temp = 0, vbat = 0, threshold;
	int fastcharge;
	int ret;

	if (!info || !ovp)
		return -EINVAL;
	*ovp = info->triggered;

	if (info->fg_type > DADA_FG_SINGLE)
		return 0;

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret)
		return ret;
	if (temp >= VBAT_OVP_TEMP_SKIP_DECIC)
		return 0;

	ret = dada_vbat_power_present(&present);
	if (ret || !present)
		return ret;

	ret = strategy_class_fg_ops_get_voltage(&vbat);
	if (ret)
		return ret;
	if (info->fake_override_mv > 0)
		vbat = info->fake_override_mv;

	fastcharge = strategy_class_fg_get_fastcharge();
	threshold = fastcharge > 0 ? info->active_ffc_mv : info->active_normal_mv;

	if (!info->triggered &&
	    vbat > threshold + info->hysteresis_mv)
		info->triggered = true;
	else if (info->triggered &&
		 vbat <= threshold - info->recharge_delta_mv)
		info->triggered = false;

	*ovp = info->triggered;
	return 0;
}

static void dada_vbat_ovp_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct dada_vbat_ovp *info = container_of(to_delayed_work(work),
						   struct dada_vbat_ovp, work);
	bool previous = info->triggered;
	bool ovp = previous;
	unsigned long delay = VBAT_OVP_NORMAL_MS;
	int ovp_status;
	int ret;

	if (info->fake_debug_mv > 0) {
		if (info->fg_type == DADA_FG_SINGLE_SERIES)
			ovp = info->fake_debug_mv > 9200;
		else
			ovp = info->fake_debug_mv > 4600;
		info->triggered = ovp;
		ret = 0;
	} else {
		ret = dada_vbat_ovp_sample(info, &ovp);
	}

	/*
	 * Stock publishes a 32-bit status word (the Dada consumer copies
	 * sizeof(int) bytes out of the notification payload).
	 */
	ovp_status = info->triggered;
	if (!ret && previous != info->triggered)
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
				       MCA_EVENT_VBAT_OVP_CHANGE,
				       &ovp_status);

	if (info->triggered)
		delay = VBAT_OVP_FAST_MS;
	schedule_delayed_work(&info->work, msecs_to_jiffies(delay));
}

static ssize_t dada_vbat_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct dada_vbat_ovp *info = dev_get_drvdata(dev);

	if (!info)
		return -ENODEV;
	if (!strcmp(attr->attr.name, "fake_vbat_for_debug"))
		return sysfs_emit(buf, "%d\n", info->fake_debug_mv);
	if (!strcmp(attr->attr.name, "fake_vbat_override"))
		return sysfs_emit(buf, "%d\n", info->fake_override_mv);
	return -EINVAL;
}

static ssize_t dada_vbat_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct dada_vbat_ovp *info = dev_get_drvdata(dev);
	int value;

	if (!info)
		return -ENODEV;
	if (kstrtoint(buf, 10, &value))
		return -EINVAL;

	if (!strcmp(attr->attr.name, "fake_vbat_for_debug"))
		info->fake_debug_mv = value;
	else if (!strcmp(attr->attr.name, "fake_vbat_override"))
		info->fake_override_mv = value;
	else
		return -EINVAL;

	mod_delayed_work(system_wq, &info->work, 0);
	return count;
}

static struct mca_sysfs_attr_info dada_vbat_attrs_info[] = {
	mca_sysfs_attr_rw(dada_vbat, 0664, DADA_VBAT_FAKE_DEBUG,
			  fake_vbat_for_debug),
	mca_sysfs_attr_rw(dada_vbat, 0664, DADA_VBAT_FAKE_OVERRIDE,
			  fake_vbat_override),
};
static struct attribute *dada_vbat_attrs[ARRAY_SIZE(dada_vbat_attrs_info) + 1];
static const struct attribute_group dada_vbat_group = { .attrs = dada_vbat_attrs };

static int dada_vbat_parse_dt(struct dada_vbat_ovp *info)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	struct device_node *np = info->dev->of_node;
	int ret = 0;

	if (!np)
		return -ENODEV;

	info->support_global_fv = of_property_read_bool(np, "support_global_fv");
	ret |= mca_parse_dts_u32(np, "vbat_ovp_threshold_ffc",
				 &info->threshold_ffc_mv,
				 VBAT_OVP_THR_FFC_DEFAULT_MV);
	ret |= mca_parse_dts_u32(np, "vbat_ovp_threshold_normal",
				 &info->threshold_normal_mv,
				 VBAT_OVP_THR_NORMAL_DEFAULT_MV);
	(void)mca_parse_dts_u32(np, "vbat_ovp_threshold_ffc_gl",
				&info->threshold_ffc_global_mv,
				VBAT_OVP_THR_FFC_GL_DEFAULT_MV);
	(void)mca_parse_dts_u32(np, "vbat_ovp_threshold_normal_gl",
				&info->threshold_normal_global_mv,
				VBAT_OVP_THR_NORMAL_GL_DEFAULT_MV);
	ret |= mca_parse_dts_u32(np, "vbat_ovp_threshold_hys",
				 &info->hysteresis_mv,
				 VBAT_OVP_HYS_DEFAULT_MV);
	ret |= mca_parse_dts_u32(np, "vbat_ovp_recharge_delta",
				 &info->recharge_delta_mv,
				 VBAT_OVP_RECHARGE_DEFAULT_MV);
	ret |= mca_parse_dts_u32(np, "fg_type", &info->fg_type,
				 DADA_FG_SINGLE);
	if (ret)
		return ret;

	if (hwid && info->support_global_fv &&
	    hwid->country_version != CountryCN) {
		info->active_ffc_mv = info->threshold_ffc_global_mv;
		info->active_normal_mv = info->threshold_normal_global_mv;
	} else {
		info->active_ffc_mv = info->threshold_ffc_mv;
		info->active_normal_mv = info->threshold_normal_mv;
	}
	return 0;
}

static int dada_vbat_ovp_probe(struct platform_device *pdev)
{
	struct dada_vbat_ovp *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	info->fake_debug_mv = -22;
	info->fake_override_mv = -22;
	platform_set_drvdata(pdev, info);
	dev_set_drvdata(&pdev->dev, info);

	ret = dada_vbat_parse_dt(info);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to parse VBAT OVP DT\n");

	mca_sysfs_init_attrs(dada_vbat_attrs, dada_vbat_attrs_info,
			     ARRAY_SIZE(dada_vbat_attrs_info));
	ret = mca_sysfs_create_link_group(SYSFS_DEV_5, "vbat_ovp", &pdev->dev,
					  &dada_vbat_group);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to create VBAT OVP sysfs\n");

	INIT_DELAYED_WORK(&info->work, dada_vbat_ovp_work);
	schedule_delayed_work(&info->work,
			      msecs_to_jiffies(VBAT_OVP_NORMAL_MS));
	mca_log_info("VBAT OVP monitor ready normal=%d ffc=%d\n",
		     info->active_normal_mv, info->active_ffc_mv);
	return 0;
}

static int dada_vbat_ovp_remove(struct platform_device *pdev)
{
	struct dada_vbat_ovp *info = platform_get_drvdata(pdev);

	if (!info)
		return 0;
	cancel_delayed_work_sync(&info->work);
	mca_sysfs_remove_link_group(SYSFS_DEV_5, "vbat_ovp", &pdev->dev,
				    &dada_vbat_group);
	return 0;
}

static const struct of_device_id dada_vbat_ovp_match[] = {
	{ .compatible = "mca,vbat_ovp_monitor" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_vbat_ovp_match);

static struct platform_driver dada_vbat_ovp_driver = {
	.driver = {
		.name = "mca_vbat_ovp_monitor",
		.of_match_table = dada_vbat_ovp_match,
	},
	.probe = dada_vbat_ovp_probe,
	.remove = dada_vbat_ovp_remove,
};
module_platform_driver(dada_vbat_ovp_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA VBAT OVP monitor");
MODULE_LICENSE("GPL v2");
