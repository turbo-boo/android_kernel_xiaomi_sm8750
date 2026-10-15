// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA software IBAT OCP monitor for Dada. */
#include <mca/common/mca_workqueue.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/strategy/strategy_fg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_ibat_ocp_mon"
#endif

#define IBAT_OCP_MASTER_DEFAULT 9280000
#define IBAT_OCP_SLAVE_DEFAULT  3540000
#define IBAT_OCP_FAST_MS         10000
#define IBAT_OCP_NORMAL_MS       60000
#define IBAT_OCP_FAKE_TRIGGER    13000000
#define DADA_FG_PARALLEL         1

enum dada_ibat_attr {
	DADA_IBAT_FAKE_DEBUG = 0,
	DADA_IBAT_FAKE_MASTER,
	DADA_IBAT_FAKE_SLAVE,
};

struct dada_ibat_ocp {
	struct device *dev;
	struct delayed_work work;
	int fg_type;
	int threshold[2];
	int fake_debug;
	int fake_master;
	int fake_slave;
	int status;
};

static int dada_ibat_power_present(bool *present)
{
	int usb = 0, wls = 0;
	int ret_usb, ret_wls;

	if (!present)
		return -EINVAL;
	ret_usb = platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &usb);
	ret_wls = platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &wls);
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

static int dada_ibat_ocp_sample(struct dada_ibat_ocp *info, int *status)
{
	bool present = false;
	int master = 0, slave = 0;
	int ret_master, ret_slave;
	int flag = 0;
	int ret;

	if (!info || !status)
		return -EINVAL;
	*status = 0;

	/* Stock monitor is defined only for dual-FG parallel batteries. */
	if (info->fg_type != DADA_FG_PARALLEL)
		return 0;

	ret = dada_ibat_power_present(&present);
	if (ret || !present)
		return ret;
	if (strategy_class_fg_get_fastcharge() <= 0)
		return 0;

	ret_master = platform_fg_ops_get_curr(FG_IC_MASTER, &master);
	ret_slave = platform_fg_ops_get_curr(FG_IC_SLAVE, &slave);
	if (ret_master || ret_slave)
		return ret_master ? ret_master : ret_slave;

	if (info->fake_master > 0)
		master = info->fake_master;
	if (info->fake_slave > 0)
		slave = info->fake_slave;

	if (abs(master) > info->threshold[FG_IC_MASTER])
		flag |= BIT(1);
	if (abs(slave) > info->threshold[FG_IC_SLAVE])
		flag |= BIT(0);
	*status = flag & 0x3;
	return 0;
}

static void dada_ibat_ocp_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct dada_ibat_ocp *info = container_of(to_delayed_work(work),
						   struct dada_ibat_ocp, work);
	int old = info->status;
	int status = 0;
	int ret;
	unsigned long delay = IBAT_OCP_NORMAL_MS;

	if (info->fake_debug > 0) {
		status = info->fake_debug > IBAT_OCP_FAKE_TRIGGER;
		ret = 0;
	} else {
		ret = dada_ibat_ocp_sample(info, &status);
	}
	if (!ret)
		info->status = status;
	if (!ret && old != info->status)
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
				       MCA_EVENT_IBAT_OCP_CHANGE,
				       &info->status);
	if (info->status)
		delay = IBAT_OCP_FAST_MS;
	schedule_delayed_work(&info->work, msecs_to_jiffies(delay));
}

static ssize_t dada_ibat_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct dada_ibat_ocp *info = dev_get_drvdata(dev);

	if (!info)
		return -ENODEV;
	if (!strcmp(attr->attr.name, "fake_ibat_for_debug"))
		return sysfs_emit(buf, "%d\n", info->fake_debug);
	if (!strcmp(attr->attr.name, "fake_master_ibat_override"))
		return sysfs_emit(buf, "%d\n", info->fake_master);
	if (!strcmp(attr->attr.name, "fake_slave_ibat_override"))
		return sysfs_emit(buf, "%d\n", info->fake_slave);
	return -EINVAL;
}

static ssize_t dada_ibat_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct dada_ibat_ocp *info = dev_get_drvdata(dev);
	int value;

	if (!info)
		return -ENODEV;
	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (!strcmp(attr->attr.name, "fake_ibat_for_debug"))
		info->fake_debug = value;
	else if (!strcmp(attr->attr.name, "fake_master_ibat_override"))
		info->fake_master = value;
	else if (!strcmp(attr->attr.name, "fake_slave_ibat_override"))
		info->fake_slave = value;
	else
		return -EINVAL;
	mod_delayed_work(system_wq, &info->work, 0);
	return count;
}

static struct mca_sysfs_attr_info dada_ibat_attrs_info[] = {
	mca_sysfs_attr_rw(dada_ibat, 0664, DADA_IBAT_FAKE_DEBUG,
			  fake_ibat_for_debug),
	mca_sysfs_attr_rw(dada_ibat, 0664, DADA_IBAT_FAKE_MASTER,
			  fake_master_ibat_override),
	mca_sysfs_attr_rw(dada_ibat, 0664, DADA_IBAT_FAKE_SLAVE,
			  fake_slave_ibat_override),
};
static struct attribute *dada_ibat_attrs[ARRAY_SIZE(dada_ibat_attrs_info) + 1];
static const struct attribute_group dada_ibat_group = { .attrs = dada_ibat_attrs };

static int dada_ibat_parse_dt(struct dada_ibat_ocp *info)
{
	struct device_node *np = info->dev->of_node;
	int ret_type, ret_threshold;

	if (!np)
		return -ENODEV;
	ret_type = mca_parse_dts_u32(np, "fg_type", &info->fg_type, 0);
	ret_threshold = mca_parse_dts_u32_array(np, "ocp_threshold",
						info->threshold, 2);
	if (ret_threshold) {
		info->threshold[FG_IC_MASTER] = IBAT_OCP_MASTER_DEFAULT;
		info->threshold[FG_IC_SLAVE] = IBAT_OCP_SLAVE_DEFAULT;
	}
	/* Keep stock defaults usable when a board omits the optional array. */
	return ret_type;
}

static int dada_ibat_ocp_probe(struct platform_device *pdev)
{
	struct dada_ibat_ocp *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	info->fake_debug = -22;
	info->fake_master = -22;
	info->fake_slave = -22;
	platform_set_drvdata(pdev, info);
	dev_set_drvdata(&pdev->dev, info);

	ret = dada_ibat_parse_dt(info);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to parse IBAT OCP DT\n");

	mca_sysfs_init_attrs(dada_ibat_attrs, dada_ibat_attrs_info,
			     ARRAY_SIZE(dada_ibat_attrs_info));
	ret = mca_sysfs_create_link_group("charger", "ibat_ocp", &pdev->dev,
					  &dada_ibat_group);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to create IBAT OCP sysfs\n");

	INIT_DELAYED_WORK(&info->work, dada_ibat_ocp_work);
	schedule_delayed_work(&info->work, msecs_to_jiffies(IBAT_OCP_NORMAL_MS));
	mca_log_info("IBAT OCP monitor ready fg_type=%d thresholds=%d/%d\n",
		     info->fg_type, info->threshold[0], info->threshold[1]);
	return 0;
}

static int dada_ibat_ocp_remove(struct platform_device *pdev)
{
	struct dada_ibat_ocp *info = platform_get_drvdata(pdev);

	if (!info)
		return 0;
	cancel_delayed_work_sync(&info->work);
	mca_sysfs_remove_link_group("charger", "ibat_ocp", &pdev->dev,
				    &dada_ibat_group);
	return 0;
}

static const struct of_device_id dada_ibat_ocp_match[] = {
	{ .compatible = "mca,ibat_ocp_monitor" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_ibat_ocp_match);

static struct platform_driver dada_ibat_ocp_driver = {
	.driver = {
		.name = "mca_ibat_ocp_monitor",
		.of_match_table = dada_ibat_ocp_match,
	},
	.probe = dada_ibat_ocp_probe,
	.remove = dada_ibat_ocp_remove,
};
module_platform_driver(dada_ibat_ocp_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA IBAT OCP monitor");
MODULE_LICENSE("GPL v2");
