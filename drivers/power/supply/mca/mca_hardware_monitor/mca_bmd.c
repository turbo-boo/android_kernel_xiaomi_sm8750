// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA battery-missing detector for Dada. */
#include <mca/common/mca_workqueue.h>
#include <linux/gpio.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_bmd"
#endif

#define DADA_BMD_BTB_COUNT       2
#define DADA_BMD_MASTER          0
#define DADA_BMD_SLAVE           1
#define DADA_BMD_MONITOR_MS      500
#define DADA_BMD_INITIAL_MS      2500
#define DADA_BMD_DUAL_FG         1

/* Values are Xiaomi's public bmd_scheme ABI. Dada DT uses <3 1>. */
enum dada_bmd_scheme {
	DADA_BMD_ADC = 0,
	DADA_BMD_GPIO = 1,
	DADA_BMD_INT = 2,
	DADA_BMD_IIC = 3,
};

enum dada_bmd_attr {
	DADA_BMD_MASTER_ATTR = 0,
	DADA_BMD_SLAVE_ATTR,
	DADA_BMD_MISSING_ATTR,
};

struct dada_bmd_channel {
	int scheme;
	int gpio;
};

struct dada_bmd {
	struct device *dev;
	struct delayed_work monitor_work;
	struct delayed_work initial_report_work;
	struct dada_bmd_channel channel[DADA_BMD_BTB_COUNT];
	bool btb_online[DADA_BMD_BTB_COUNT];
	bool batt_missing;
	bool fake_batt;
	int fg_type;
};

static bool dada_bmd_read_channel(struct dada_bmd *info, int index)
{
	int ret;

	if (!info || index < 0 || index >= DADA_BMD_BTB_COUNT)
		return false;

	switch (info->channel[index].scheme) {
	case DADA_BMD_IIC:
		if (info->fg_type == DADA_BMD_DUAL_FG)
			ret = strategy_class_fg_dual_is_chip_ok(index);
		else
			ret = strategy_class_fg_is_chip_ok();
		return ret > 0;
	case DADA_BMD_GPIO:
		if (!gpio_is_valid(info->channel[index].gpio))
			return false;
		/* Xiaomi public BMD treats active-low BTB GPIO as connected. */
		return !gpio_get_value(info->channel[index].gpio);
	default:
		/* Dada stock DT does not select ADC/INT for this node. */
		return false;
	}
}

static void dada_bmd_sample(struct dada_bmd *info, bool report)
{
	bool missing = true;
	bool fake;
	int payload;

	info->btb_online[DADA_BMD_MASTER] =
		dada_bmd_read_channel(info, DADA_BMD_MASTER);
	info->btb_online[DADA_BMD_SLAVE] =
		dada_bmd_read_channel(info, DADA_BMD_SLAVE);

	if (info->btb_online[DADA_BMD_MASTER])
		missing = !info->btb_online[DADA_BMD_SLAVE];

	if (report || missing != info->batt_missing) {
		bool changed = missing != info->batt_missing;

		info->batt_missing = missing;
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
				       MCA_EVENT_BATT_BTB_CHANGE,
				       &info->batt_missing);
		if (changed) {
			if (!missing) {
				mca_charge_mievent_set_state(
					MIEVENT_STATE_END,
					info->fg_type == DADA_BMD_DUAL_FG ?
					CHARGE_DFX_DUAL_BATTERY_MISSING :
					CHARGE_DFX_BATTERY_MISSING);
			} else if (info->fg_type == DADA_BMD_DUAL_FG) {
				payload = info->btb_online[DADA_BMD_MASTER];
				mca_charge_mievent_report(
					CHARGE_DFX_DUAL_BATTERY_MISSING,
					&payload, 1);
			} else {
				mca_charge_mievent_report(
					CHARGE_DFX_BATTERY_MISSING, NULL, 0);
			}
		}
	}

	if (info->fg_type == DADA_BMD_DUAL_FG)
		fake = !info->btb_online[DADA_BMD_MASTER] ||
		       !info->btb_online[DADA_BMD_SLAVE];
	else
		fake = !info->btb_online[DADA_BMD_MASTER] &&
		       !info->btb_online[DADA_BMD_SLAVE];

	if (report || fake != info->fake_batt) {
		info->fake_batt = fake;
		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
				       MCA_EVENT_BATTERY_FAKE_POWER,
				       &info->fake_batt);
	}
}

static void dada_bmd_monitor_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct dada_bmd *info = container_of(to_delayed_work(work),
						     struct dada_bmd,
						     monitor_work);

	dada_bmd_sample(info, false);
	schedule_delayed_work(&info->monitor_work,
			      msecs_to_jiffies(DADA_BMD_MONITOR_MS));
}

static void dada_bmd_initial_report_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct dada_bmd *info = container_of(to_delayed_work(work),
						     struct dada_bmd,
						     initial_report_work);

	dada_bmd_sample(info, true);
}

static int dada_bmd_get_status(int type, void *value, void *data)
{
	struct dada_bmd *info = data;

	if (!info || !value || type != STRATEGY_STATUS_TYPE_BMD_BATT_MISSING)
		return -EINVAL;
	*(u32 *)value = info->batt_missing;
	return 0;
}

static int dada_bmd_process_event(int event, int value, void *data)
{
	struct dada_bmd *info = data;

	if (!info)
		return -EINVAL;
	/* Re-sample immediately around charger topology changes. */
	switch (event) {
	case MCA_EVENT_USB_CONNECT:
	case MCA_EVENT_USB_DISCONNECT:
	case MCA_EVENT_WIRELESS_CONNECT:
	case MCA_EVENT_WIRELESS_DISCONNECT:
		mod_delayed_work(system_wq, &info->monitor_work, 0);
		break;
	default:
		break;
	}
	return 0;
}

static ssize_t dada_bmd_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct dada_bmd *info = dev_get_drvdata(dev);

	if (!info)
		return -ENODEV;
	/*
	 * Stock re-reads the BTB channels on every attribute access instead of
	 * returning the last sampled cache.
	 */
	if (!strcmp(attr->attr.name, "btb_master_status"))
		return sysfs_emit(buf, "%d\n",
				  dada_bmd_read_channel(info, DADA_BMD_MASTER));
	if (!strcmp(attr->attr.name, "btb_slave_status"))
		return sysfs_emit(buf, "%d\n",
				  dada_bmd_read_channel(info, DADA_BMD_SLAVE));
	if (!strcmp(attr->attr.name, "btb_missing_status"))
		return sysfs_emit(buf, "%d\n", info->batt_missing);
	return -EINVAL;
}

static struct mca_sysfs_attr_info dada_bmd_attrs_info[] = {
	mca_sysfs_attr_ro(dada_bmd, 0440, DADA_BMD_MASTER_ATTR,
			  btb_master_status),
	mca_sysfs_attr_ro(dada_bmd, 0440, DADA_BMD_SLAVE_ATTR,
			  btb_slave_status),
	mca_sysfs_attr_ro(dada_bmd, 0440, DADA_BMD_MISSING_ATTR,
			  btb_missing_status),
};
static struct attribute *dada_bmd_attrs[ARRAY_SIZE(dada_bmd_attrs_info) + 1];
static const struct attribute_group dada_bmd_group = { .attrs = dada_bmd_attrs };

static int dada_bmd_parse_dt(struct dada_bmd *info)
{
	struct device_node *np = info->dev->of_node;
	int scheme[DADA_BMD_BTB_COUNT] = { 0 };
	int gpio, i, ret;

	if (!np)
		return -ENODEV;
	ret = mca_parse_dts_u32_array(np, "btb_bmd_scheme", scheme,
					      DADA_BMD_BTB_COUNT);
	if (ret)
		return ret;
	(void)mca_parse_dts_u32(np, "fg_type", &info->fg_type, 0);

	for (i = 0; i < DADA_BMD_BTB_COUNT; i++) {
		info->channel[i].scheme = scheme[i];
		info->channel[i].gpio = -EINVAL;
		if (scheme[i] != DADA_BMD_GPIO)
			continue;
		gpio = of_get_named_gpio(np, "btb_gpio", 0);
		if (!gpio_is_valid(gpio))
			return gpio < 0 ? gpio : -EINVAL;
		ret = devm_gpio_request_one(info->dev, gpio, GPIOF_IN,
					    "mca-btb-detect");
		if (ret)
			return ret;
		info->channel[i].gpio = gpio;
	}

	/* Refuse silent fallback if another board selects an unimplemented scheme. */
	for (i = 0; i < DADA_BMD_BTB_COUNT; i++)
		if (info->channel[i].scheme != DADA_BMD_IIC &&
		    info->channel[i].scheme != DADA_BMD_GPIO)
			return -EOPNOTSUPP;
	return 0;
}

static int dada_bmd_probe(struct platform_device *pdev)
{
	struct dada_bmd *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	dev_set_drvdata(&pdev->dev, info);

	ret = dada_bmd_parse_dt(info);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to parse BMD DT\n");

	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_BMD,
					dada_bmd_process_event,
					dada_bmd_get_status, NULL, info);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register BMD strategy\n");

	mca_sysfs_init_attrs(dada_bmd_attrs, dada_bmd_attrs_info,
			     ARRAY_SIZE(dada_bmd_attrs_info));
	ret = mca_sysfs_create_link_group(SYSFS_DEV_5, "bmd", &pdev->dev,
					  &dada_bmd_group);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to create BMD sysfs\n");

	INIT_DELAYED_WORK(&info->monitor_work, dada_bmd_monitor_work);
	INIT_DELAYED_WORK(&info->initial_report_work, dada_bmd_initial_report_work);
	schedule_delayed_work(&info->monitor_work,
			      msecs_to_jiffies(DADA_BMD_MONITOR_MS));
	schedule_delayed_work(&info->initial_report_work,
			      msecs_to_jiffies(DADA_BMD_INITIAL_MS));
	mca_log_info("BMD ready scheme=%d/%d fg_type=%d\n",
		     info->channel[0].scheme, info->channel[1].scheme,
		     info->fg_type);
	return 0;
}

static int dada_bmd_remove(struct platform_device *pdev)
{
	struct dada_bmd *info = platform_get_drvdata(pdev);

	if (!info)
		return 0;
	cancel_delayed_work_sync(&info->initial_report_work);
	cancel_delayed_work_sync(&info->monitor_work);
	mca_sysfs_remove_link_group(SYSFS_DEV_5, "bmd", &pdev->dev,
				    &dada_bmd_group);
	return 0;
}

static const struct of_device_id dada_bmd_match[] = {
	{ .compatible = "mca,bmd" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_bmd_match);

static struct platform_driver dada_bmd_driver = {
	.driver = {
		.name = "mca_bmd",
		.of_match_table = dada_bmd_match,
	},
	.probe = dada_bmd_probe,
	.remove = dada_bmd_remove,
};
module_platform_driver(dada_bmd_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA battery missing detector");
MODULE_LICENSE("GPL v2");
