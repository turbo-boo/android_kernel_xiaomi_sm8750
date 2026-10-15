// SPDX-License-Identifier: GPL-2.0
#include <mca/common/mca_callback.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include <mca/common/mca_charge_interface.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_charge_if"
#endif

#define MCA_CHARGE_IF_MAX_RW_BUFF 256

enum mca_charge_if_sysfs_type {
	MCA_CHARGE_IF_SYSFS_INPUT_SUSPEND = 0,
	MCA_CHARGE_IF_SYSFS_CHARGE_ENABLE,
	MCA_CHARGE_IF_SYSFS_IIN_LIMIT,
	MCA_CHARGE_IF_SYSFS_ICHG_LIMIT,
	MCA_CHARGE_IF_SYSFS_POWER_LIMIT,
	MCA_CHARGE_IF_SYSFS_SUSPEND_STATUS,
	MCA_CHARGE_IF_SYSFS_SHIPMODE,
};

struct mca_charge_if_dev {
	struct device *dev;
};

static const char * const mca_charge_if_type_names[MCA_CHARGE_IF_CHG_TYPE_END] = {
	[MCA_CHARGE_IF_CHG_TYPE_BUCK] = "buck",
	[MCA_CHARGE_IF_CHG_TYPE_MAIN_BUCK] = "main_buck",
	[MCA_CHARGE_IF_CHG_TYPE_AUX_BUCK] = "aux_buck",
	[MCA_CHARGE_IF_CHG_TYPE_QC] = "quick",
	[MCA_CHARGE_IF_CHG_TYPE_QC_MAIN_PATH] = "quick_main",
	[MCA_CHARGE_IF_CHG_TYPE_QC_AUX_PATH] = "quick_aux",
	[MCA_CHARGE_IF_CHG_TYPE_QC_DIV1] = "div1",
	[MCA_CHARGE_IF_CHG_TYPE_QC_DIV2] = "div2",
	[MCA_CHARGE_IF_CHG_TYPE_QC_DIV4] = "div4",
	[MCA_CHARGE_IF_CHG_TYPE_WL_BUCK] = "wl_buck",
	[MCA_CHARGE_IF_CHG_TYPE_WL_MAIN_BUCK] = "wl_main_buck",
	[MCA_CHARGE_IF_CHG_TYPE_WL_AUX_BUCK] = "wl_aux_buck",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC] = "wl_quick",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_MAIN_PATH] = "wl_quick_main",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_AUX_PATH] = "wl_quick_aux",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV1] = "wl_div1",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV2] = "wl_div2",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV4] = "wl_div4",
	[MCA_CHARGE_IF_CHG_TYPE_ALL] = "all",
};

static struct mca_charge_if_ops *mca_charge_if_ops[MCA_CHARGE_IF_CHG_TYPE_END];
static DEFINE_MUTEX(mca_charge_if_lock);
DEFINE_STATIC_SRCU(mca_charge_if_callbacks);

static int mca_charge_if_type_from_name(const char *name)
{
	int i;

	if (!name)
		return -EINVAL;
	for (i = 0; i < MCA_CHARGE_IF_CHG_TYPE_END; i++)
		if (mca_charge_if_type_names[i] &&
		    !strcmp(name, mca_charge_if_type_names[i]))
			return i;
	return -ENOENT;
}

int mca_charge_if_ops_register(struct mca_charge_if_ops *ops)
{
	int type;

	if (!ops || !ops->type_name)
		return -EINVAL;
	type = mca_charge_if_type_from_name(ops->type_name);
	if (type < 0 || type >= MCA_CHARGE_IF_CHG_TYPE_ALL)
		return -EINVAL;

	mutex_lock(&mca_charge_if_lock);
	rcu_assign_pointer(mca_charge_if_ops[type], ops);
	mutex_unlock(&mca_charge_if_lock);
	return 0;
}
EXPORT_SYMBOL(mca_charge_if_ops_register);

void mca_charge_if_ops_unregister(struct mca_charge_if_ops *ops)
{
	int type;

	if (!ops)
		return;
	type = mca_charge_if_type_from_name(ops->type_name);
	if (type < 0 || type >= MCA_CHARGE_IF_CHG_TYPE_ALL)
		return;
	mutex_lock(&mca_charge_if_lock);
	if (mca_charge_if_ops[type] == ops)
		rcu_assign_pointer(mca_charge_if_ops[type], NULL);
	mutex_unlock(&mca_charge_if_lock);
	synchronize_srcu(&mca_charge_if_callbacks);
}
EXPORT_SYMBOL(mca_charge_if_ops_unregister);

static struct mca_charge_if_ops *mca_charge_if_get_ops(int type)
{
	struct mca_charge_if_ops *ops = NULL;

	if (type < 0 || type >= MCA_CHARGE_IF_CHG_TYPE_ALL)
		return NULL;
	mutex_lock(&mca_charge_if_lock);
	ops = srcu_dereference(mca_charge_if_ops[type], &mca_charge_if_callbacks);
	mutex_unlock(&mca_charge_if_lock);
	return ops;
}

static int mca_charge_if_set_one(const char *user, int type, int attr,
				char *value)
{
	CLASS(mca_callback, callback_scope)(&mca_charge_if_callbacks);

	struct mca_charge_if_ops *ops = mca_charge_if_get_ops(type);
	unsigned int uvalue = 0;

	if (!ops)
		return 0;

	switch (attr) {
	case MCA_CHARGE_IF_SYSFS_INPUT_SUSPEND:
		return ops->set_input_suspend ?
			ops->set_input_suspend(user, value, ops->data) : 0;
	case MCA_CHARGE_IF_SYSFS_CHARGE_ENABLE:
		if (kstrtouint(value, 0, &uvalue))
			return -EINVAL;
		return ops->set_charge_enable ?
			ops->set_charge_enable(user, uvalue, ops->data) : 0;
	case MCA_CHARGE_IF_SYSFS_IIN_LIMIT:
		return ops->set_input_current_limit ?
			ops->set_input_current_limit(user, value, ops->data) : 0;
	case MCA_CHARGE_IF_SYSFS_ICHG_LIMIT:
		return ops->set_charge_current_limit ?
			ops->set_charge_current_limit(user, value, ops->data) : 0;
	case MCA_CHARGE_IF_SYSFS_POWER_LIMIT:
		if (kstrtouint(value, 0, &uvalue))
			return -EINVAL;
		return ops->set_charge_power_limit ?
			ops->set_charge_power_limit(user, uvalue, ops->data) : 0;
	case MCA_CHARGE_IF_SYSFS_SHIPMODE:
		if (kstrtouint(value, 0, &uvalue))
			return -EINVAL;
		return ops->set_ship_mode_en ?
			ops->set_ship_mode_en(user, uvalue, ops->data) : 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int mca_charge_if_set(const char *user, int type, int attr, char *value)
{
	int i, ret = 0;

	if (type == MCA_CHARGE_IF_CHG_TYPE_ALL) {
		for (i = 0; i < MCA_CHARGE_IF_CHG_TYPE_ALL; i++) {
			int rc = mca_charge_if_set_one(user, i, attr, value);
			if (rc && !ret)
				ret = rc;
		}
	} else {
		ret = mca_charge_if_set_one(user, type, attr, value);
	}

	if (!ret && attr == MCA_CHARGE_IF_SYSFS_INPUT_SUSPEND)
		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
				       MCA_EVENT_BATTERY_STS_CHANGE, NULL);
	return ret;
}

static int mca_charge_if_get_one(int type, int attr, char *value, size_t size)
{
	CLASS(mca_callback, callback_scope)(&mca_charge_if_callbacks);

	struct mca_charge_if_ops *ops = mca_charge_if_get_ops(type);

	if (!ops || !value || !size)
		return -ENODEV;
	value[0] = '\0';

	switch (attr) {
	case MCA_CHARGE_IF_SYSFS_INPUT_SUSPEND:
		return ops->get_input_suspend ?
			ops->get_input_suspend(value, ops->data) : -EOPNOTSUPP;
	case MCA_CHARGE_IF_SYSFS_CHARGE_ENABLE:
		return ops->get_charge_enable ?
			ops->get_charge_enable(value, ops->data) : -EOPNOTSUPP;
	case MCA_CHARGE_IF_SYSFS_IIN_LIMIT:
		return ops->get_input_current_limit ?
			ops->get_input_current_limit(value, ops->data) : -EOPNOTSUPP;
	case MCA_CHARGE_IF_SYSFS_ICHG_LIMIT:
		return ops->get_charge_current_limit ?
			ops->get_charge_current_limit(value, ops->data) : -EOPNOTSUPP;
	case MCA_CHARGE_IF_SYSFS_POWER_LIMIT:
		return ops->get_charge_power_limit ?
			ops->get_charge_power_limit(value, ops->data) : -EOPNOTSUPP;
	default:
		return -EOPNOTSUPP;
	}
}

static bool mca_charge_if_suspend_status(void)
{
	char value[MCA_CHARGE_IF_MAX_VALUE_BUFF];
	int i;

	for (i = 0; i < MCA_CHARGE_IF_CHG_TYPE_ALL; i++) {
		size_t len;

		if (mca_charge_if_get_one(i, MCA_CHARGE_IF_SYSFS_INPUT_SUSPEND,
					  value, sizeof(value)))
			continue;
		len = strnlen(value, sizeof(value));
		while (len && (value[len - 1] == '\n' || value[len - 1] == ' ' ||
			       value[len - 1] == '\t'))
			len--;
		if (len && value[len - 1] != '0')
			return true;
	}
	return false;
}

static ssize_t mca_charge_if_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	CLASS(mca_callback, callback_scope)(&mca_charge_if_callbacks);

	struct mca_sysfs_attr_info *info =
		container_of(attr, struct mca_sysfs_attr_info, attr);
	char value[MCA_CHARGE_IF_MAX_VALUE_BUFF];
	ssize_t pos = 0;
	int i;

	if (info->sysfs_attr_name == MCA_CHARGE_IF_SYSFS_SUSPEND_STATUS)
		return sysfs_emit(buf, "%d\n", mca_charge_if_suspend_status());
	if (info->sysfs_attr_name == MCA_CHARGE_IF_SYSFS_SHIPMODE) {
		struct mca_charge_if_ops *ops =
			mca_charge_if_get_ops(MCA_CHARGE_IF_CHG_TYPE_BUCK);
		bool enabled = false;
		int ret;

		if (!ops || !ops->get_ship_mode_status)
			return -ENODEV;
		ret = ops->get_ship_mode_status(&enabled, ops->data);
		return ret ? ret : sysfs_emit(buf, "%d\n", enabled);
	}

	for (i = 0; i < MCA_CHARGE_IF_CHG_TYPE_ALL; i++) {
		if (mca_charge_if_get_one(i, info->sysfs_attr_name,
					  value, sizeof(value)))
			continue;
		pos += sysfs_emit_at(buf, pos, "%s %s\n",
				     mca_charge_if_type_names[i], value);
		if (pos >= PAGE_SIZE - 1)
			break;
	}
	return pos;
}

static ssize_t mca_charge_if_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *info =
		container_of(attr, struct mca_sysfs_attr_info, attr);
	char user[64] = { 0 };
	char type_name[64] = { 0 };
	char value[MCA_CHARGE_IF_MAX_VALUE_BUFF] = { 0 };
	int type, ret;

	if (info->sysfs_attr_name == MCA_CHARGE_IF_SYSFS_SUSPEND_STATUS)
		return -EACCES;

	if (sscanf(buf, "%63s %63s %127s", user, type_name, value) != 3)
		return -EINVAL;
	type = mca_charge_if_type_from_name(type_name);
	if (type < 0)
		return type;
	ret = mca_charge_if_set(user, type, info->sysfs_attr_name, value);
	return ret ? ret : count;
}

static struct mca_sysfs_attr_info mca_charge_if_attrs_info[] = {
	mca_sysfs_attr_rw(mca_charge_if, 0640,
			  MCA_CHARGE_IF_SYSFS_INPUT_SUSPEND, input_suspend),
	mca_sysfs_attr_rw(mca_charge_if, 0640,
			  MCA_CHARGE_IF_SYSFS_CHARGE_ENABLE, charge_enable),
	mca_sysfs_attr_rw(mca_charge_if, 0640,
			  MCA_CHARGE_IF_SYSFS_IIN_LIMIT, iin_limit),
	mca_sysfs_attr_rw(mca_charge_if, 0640,
			  MCA_CHARGE_IF_SYSFS_ICHG_LIMIT, ichg_limit),
	mca_sysfs_attr_rw(mca_charge_if, 0640,
			  MCA_CHARGE_IF_SYSFS_POWER_LIMIT, power_limit),
	mca_sysfs_attr_ro(mca_charge_if, 0440,
			  MCA_CHARGE_IF_SYSFS_SUSPEND_STATUS, suspend_status),
	mca_sysfs_attr_rw(mca_charge_if, 0640,
			  MCA_CHARGE_IF_SYSFS_SHIPMODE, shipmode_count_reset),
};

static struct attribute *mca_charge_if_attrs[
	ARRAY_SIZE(mca_charge_if_attrs_info) + 1];
static const struct attribute_group mca_charge_if_group = {
	.attrs = mca_charge_if_attrs,
};

static int mca_charge_if_probe(struct platform_device *pdev)
{
	struct mca_charge_if_dev *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	mca_sysfs_init_attrs(mca_charge_if_attrs, mca_charge_if_attrs_info,
			     ARRAY_SIZE(mca_charge_if_attrs_info));
	ret = mca_sysfs_create_link_group(SYSFS_DEV_1, "charge_interface",
					  info->dev, &mca_charge_if_group);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to create charge_interface sysfs\n");
	return 0;
}

static int mca_charge_if_remove(struct platform_device *pdev)
{
	mca_sysfs_remove_link_group(SYSFS_DEV_1, "charge_interface",
				    &pdev->dev, &mca_charge_if_group);
	return 0;
}

static const struct of_device_id mca_charge_if_match[] = {
	{ .compatible = "mca,charge_interface" },
	{},
};
MODULE_DEVICE_TABLE(of, mca_charge_if_match);

static struct platform_driver mca_charge_if_driver = {
	.driver = {
		.name = "mca_charge_interface",
		.of_match_table = mca_charge_if_match,
	},
	.probe = mca_charge_if_probe,
	.remove = mca_charge_if_remove,
};
module_platform_driver(mca_charge_if_driver);

MODULE_DESCRIPTION("Xiaomi MCA charge interface dispatcher");
MODULE_LICENSE("GPL v2");
