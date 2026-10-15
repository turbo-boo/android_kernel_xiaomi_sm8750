// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA charging path controller for Dada.
 *
 * The Dada DT overlay carries a condition->path matrix.  Each path selects a
 * boost-source combination and a list of gate states.  Dada only gives a live
 * control method to OVPGATE (CP-chip scheme); the other gate roles are marked
 * "null" in control_scheme and are therefore intentionally left untouched.
 */
#include <mca/common/mca_workqueue.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/hardware/hw_path_control.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/strategy/strategy_wireless_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_path_control"
#endif

#define DADA_PATH_MAX_ROUTES          16
#define DADA_PATH_MAX_SCHEMES         5
#define DADA_PATH_ROUTE_FIELDS        2
#define DADA_PATH_BOOST_FIELDS        5
#define DADA_PATH_GATE_FIELDS         2
#define DADA_PATH_SCHEME_FIELDS       3
#define DADA_PATH_INIT_DELAY_MS       10000

#define DADA_GATE_OVPGATE             0
#define DADA_SCHEME_CP_CHIP           2
#define DADA_BOOST_TYPE_OTG           0
#define DADA_BOOST_TYPE_REV           1

struct dada_path_route {
	int condition;
	const char *path_property;
};

struct dada_path_control {
	struct device *dev;
	struct mutex lock;
	struct delayed_work init_work;
	struct dada_path_route route[DADA_PATH_MAX_ROUTES];
	int route_count;
	int condition;
	int gate_state;
	int cp_role;
	int otg_boost_src;
	int rev_boost_src;
};

static struct dada_path_control *g_path;

enum dada_path_attr {
	DADA_PATH_USB = 0,
	DADA_PATH_WLS,
	DADA_PATH_WLS_REV,
	DADA_PATH_OTG,
	DADA_PATH_VDD,
	DADA_PATH_TOTAL,
};

static int dada_path_parse_int_string(struct device_node *np,
				      const char *property, int index,
				      int *value)
{
	const char *str;
	int ret;

	if (!np || !property || !value)
		return -EINVAL;
	ret = of_property_read_string_index(np, property, index, &str);
	if (ret)
		return ret;
	return kstrtoint(str, 10, value);
}

static int dada_path_parse_routes(struct dada_path_control *info)
{
	struct device_node *np = info->dev->of_node;
	int count, i, ret;
	const char *path;

	count = of_property_count_strings(np, "path_condition");
	if (count <= 0 || count % DADA_PATH_ROUTE_FIELDS)
		return -EINVAL;
	if (count / DADA_PATH_ROUTE_FIELDS > DADA_PATH_MAX_ROUTES)
		return -E2BIG;

	info->route_count = count / DADA_PATH_ROUTE_FIELDS;
	for (i = 0; i < info->route_count; i++) {
		ret = dada_path_parse_int_string(np, "path_condition",
						 i * DADA_PATH_ROUTE_FIELDS,
						 &info->route[i].condition);
		if (ret)
			return ret;
		ret = of_property_read_string_index(np, "path_condition",
						    i * DADA_PATH_ROUTE_FIELDS + 1,
						    &path);
		if (ret)
			return ret;
		info->route[i].path_property = path;
	}
	return 0;
}

static int dada_path_parse_ovpgate_scheme(struct dada_path_control *info)
{
	struct device_node *np = info->dev->of_node;
	const char *process;
	int count, i, role, scheme, ret;

	count = of_property_count_strings(np, "control_scheme");
	if (count <= 0 || count % DADA_PATH_SCHEME_FIELDS)
		return -EINVAL;
	if (count / DADA_PATH_SCHEME_FIELDS > DADA_PATH_MAX_SCHEMES)
		return -E2BIG;

	for (i = 0; i < count / DADA_PATH_SCHEME_FIELDS; i++) {
		ret = dada_path_parse_int_string(np, "control_scheme",
						 i * DADA_PATH_SCHEME_FIELDS,
						 &role);
		if (ret)
			return ret;
		ret = dada_path_parse_int_string(np, "control_scheme",
						 i * DADA_PATH_SCHEME_FIELDS + 1,
						 &scheme);
		if (ret)
			return ret;
		ret = of_property_read_string_index(np, "control_scheme",
						    i * DADA_PATH_SCHEME_FIELDS + 2,
						    &process);
		if (ret)
			return ret;
		if (role != DADA_GATE_OVPGATE)
			continue;
		if (scheme != DADA_SCHEME_CP_CHIP || !strcmp(process, "null"))
			return -EOPNOTSUPP;
		ret = of_property_read_u32(np, process, &info->cp_role);
		if (ret)
			return ret;
		return 0;
	}
	return -ENOENT;
}

static int dada_path_seed_boost_sources(struct dada_path_control *info)
{
	struct device_node *np = info->dev->of_node;
	const char *property;
	int type0, src0, type1, src1;
	int ret;

	if (!info->route_count)
		return -EINVAL;
	property = info->route[0].path_property;
	if (of_property_count_strings(np, property) < DADA_PATH_BOOST_FIELDS)
		return -EINVAL;

	ret = dada_path_parse_int_string(np, property, 0, &type0);
	ret |= dada_path_parse_int_string(np, property, 1, &src0);
	ret |= dada_path_parse_int_string(np, property, 2, &type1);
	ret |= dada_path_parse_int_string(np, property, 3, &src1);
	if (ret)
		return ret;
	if (type0 != DADA_BOOST_TYPE_OTG || type1 != DADA_BOOST_TYPE_REV)
		return -EINVAL;
	info->otg_boost_src = src0;
	info->rev_boost_src = src1;
	return 0;
}

static int dada_path_refresh_boost_sources(struct dada_path_control *info)
{
	int source;

	if (!platform_class_buckchg_ops_get_otg_boost_src(MAIN_BUCK_CHARGER,
							   &source))
		info->otg_boost_src = source;
	if (!mca_wireless_rev_get_rev_boost_default(&source))
		info->rev_boost_src = source;
	return 0;
}

static int dada_path_apply_gate_property(struct dada_path_control *info,
					 const char *property, int condition)
{
	struct device_node *np = info->dev->of_node;
	int count, i, role, enable, ret;

	count = of_property_count_strings(np, property);
	if (count <= 0 || count % DADA_PATH_GATE_FIELDS)
		return -EINVAL;

	for (i = 0; i < count / DADA_PATH_GATE_FIELDS; i++) {
		ret = dada_path_parse_int_string(np, property,
						 i * DADA_PATH_GATE_FIELDS,
						 &role);
		ret |= dada_path_parse_int_string(np, property,
						  i * DADA_PATH_GATE_FIELDS + 1,
						  &enable);
		if (ret)
			return ret;

		/* Dada control_scheme gives only OVPGATE a live control method. */
		if (role != DADA_GATE_OVPGATE)
			continue;
		enable = !!enable;
		if (info->gate_state == enable)
			continue;
		ret = platform_class_cp_enable_ovpgate(info->cp_role, enable);
		if (ret)
			return ret;
		info->gate_state = enable;
		mca_log_info("OVPGATE role=%d enable=%d condition=%d\n",
			     info->cp_role, enable, condition);
	}
	return 0;
}

static int dada_path_apply(struct dada_path_control *info, int condition)
{
	struct device_node *np = info->dev->of_node;
	const char *path_property = NULL;
	const char *gate_property;
	int i, groups, g;
	int type0, src0, type1, src1;
	int ret;

	for (i = 0; i < info->route_count; i++) {
		if (info->route[i].condition == condition) {
			path_property = info->route[i].path_property;
			break;
		}
	}
	if (!path_property)
		return -ENOENT;

	dada_path_refresh_boost_sources(info);
	groups = of_property_count_strings(np, path_property);
	if (groups <= 0 || groups % DADA_PATH_BOOST_FIELDS)
		return -EINVAL;
	groups /= DADA_PATH_BOOST_FIELDS;

	for (g = 0; g < groups; g++) {
		int base = g * DADA_PATH_BOOST_FIELDS;

		ret = dada_path_parse_int_string(np, path_property, base, &type0);
		ret |= dada_path_parse_int_string(np, path_property, base + 1, &src0);
		ret |= dada_path_parse_int_string(np, path_property, base + 2, &type1);
		ret |= dada_path_parse_int_string(np, path_property, base + 3, &src1);
		if (ret)
			return ret;
		if (type0 != DADA_BOOST_TYPE_OTG || type1 != DADA_BOOST_TYPE_REV)
			continue;
		if (src0 != info->otg_boost_src || src1 != info->rev_boost_src)
			continue;
		ret = of_property_read_string_index(np, path_property, base + 4,
						    &gate_property);
		if (ret)
			return ret;
		return dada_path_apply_gate_property(info, gate_property, condition);
	}

	mca_log_err("no route for condition=%d boost=%d/%d\n",
		    condition, info->otg_boost_src, info->rev_boost_src);
	return -ENOENT;
}

int mca_path_control_enable_gate(CONTROL_SRC src, bool enable)
{
	struct dada_path_control *info = READ_ONCE(g_path);
	int ret;

	if (!info)
		return -ENODEV;
	if (src != PATH_CONTROL_USB && src != PATH_CONTROL_WLS &&
	    src != PATH_CONTROL_WLS_REV && src != PATH_CONTROL_OTG &&
	    src != PATH_CONTROL_VDD)
		return -EINVAL;

	mutex_lock(&info->lock);
	if (enable)
		info->condition |= src;
	else
		info->condition &= ~src;
	ret = dada_path_apply(info, info->condition);
	mutex_unlock(&info->lock);
	return ret;
}
EXPORT_SYMBOL(mca_path_control_enable_gate);

static void dada_path_init_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct dada_path_control *info = container_of(to_delayed_work(work),
						      struct dada_path_control,
						      init_work);
	int usb = 0, wls = 0, otg = 0;
	bool reverse = false;

	mutex_lock(&info->lock);
	info->condition = 0;
	if (!platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &usb) && usb)
		info->condition |= PATH_CONTROL_USB;
	if (!platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &wls) && wls)
		info->condition |= PATH_CONTROL_WLS;
	if (!mca_wireless_rev_get_reverse_chg(&reverse) && reverse)
		info->condition |= PATH_CONTROL_WLS_REV;
	if (!platform_class_buckchg_ops_get_otg_boost_enable_status(
		    MAIN_BUCK_CHARGER, &otg) && otg != OTG_DISABLE)
		info->condition |= PATH_CONTROL_OTG;
	(void)dada_path_apply(info, info->condition);
	mutex_unlock(&info->lock);
}

static ssize_t dada_path_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct dada_path_control *info = dev_get_drvdata(dev);
	int value;

	if (!info)
		return -ENODEV;
	mutex_lock(&info->lock);
	if (!strcmp(attr->attr.name, "usb_in"))
		value = info->condition & PATH_CONTROL_USB;
	else if (!strcmp(attr->attr.name, "wls_in"))
		value = info->condition & PATH_CONTROL_WLS;
	else if (!strcmp(attr->attr.name, "wls_rev"))
		value = info->condition & PATH_CONTROL_WLS_REV;
	else if (!strcmp(attr->attr.name, "otg_in"))
		value = info->condition & PATH_CONTROL_OTG;
	else if (!strcmp(attr->attr.name, "wls_vdd"))
		value = info->condition & PATH_CONTROL_VDD;
	else if (!strcmp(attr->attr.name, "control_value"))
		value = info->condition;
	else {
		mutex_unlock(&info->lock);
		return -EINVAL;
	}
	mutex_unlock(&info->lock);
	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t dada_path_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int value;
	CONTROL_SRC src;
	struct dada_path_control *info = dev_get_drvdata(dev);

	if (!info)
		return -ENODEV;
	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (!strcmp(attr->attr.name, "control_value")) {
		/* Stock applies a one-shot override without changing source bits. */
		mutex_lock(&info->lock);
		dada_path_apply(info, value);
		mutex_unlock(&info->lock);
		return count;
	}
	if (!strcmp(attr->attr.name, "usb_in"))
		src = PATH_CONTROL_USB;
	else if (!strcmp(attr->attr.name, "wls_in"))
		src = PATH_CONTROL_WLS;
	else if (!strcmp(attr->attr.name, "wls_rev"))
		src = PATH_CONTROL_WLS_REV;
	else if (!strcmp(attr->attr.name, "otg_in"))
		src = PATH_CONTROL_OTG;
	else if (!strcmp(attr->attr.name, "wls_vdd"))
		src = PATH_CONTROL_VDD;
	else
		return -EACCES;

	/* A parsed write accepts the source state, even without a live route. */
	mca_path_control_enable_gate(src, !!value);
	return count;
}

static struct mca_sysfs_attr_info dada_path_attrs_info[] = {
	mca_sysfs_attr_rw(dada_path, 0664, DADA_PATH_USB, usb_in),
	mca_sysfs_attr_rw(dada_path, 0664, DADA_PATH_WLS, wls_in),
	mca_sysfs_attr_rw(dada_path, 0664, DADA_PATH_WLS_REV, wls_rev),
	mca_sysfs_attr_rw(dada_path, 0664, DADA_PATH_OTG, otg_in),
	mca_sysfs_attr_rw(dada_path, 0664, DADA_PATH_VDD, wls_vdd),
	mca_sysfs_attr_rw(dada_path, 0664, DADA_PATH_TOTAL, control_value),
};
static struct attribute *dada_path_attrs[ARRAY_SIZE(dada_path_attrs_info) + 1];
static const struct attribute_group dada_path_group = { .attrs = dada_path_attrs };

static int dada_path_probe(struct platform_device *pdev)
{
	struct dada_path_control *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	info->gate_state = -1;
	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);
	dev_set_drvdata(&pdev->dev, info);

	ret = dada_path_parse_routes(info);
	if (ret)
		goto err_mutex;
	ret = dada_path_parse_ovpgate_scheme(info);
	if (ret)
		goto err_mutex;
	ret = dada_path_seed_boost_sources(info);
	if (ret)
		goto err_mutex;

	mca_sysfs_init_attrs(dada_path_attrs, dada_path_attrs_info,
			     ARRAY_SIZE(dada_path_attrs_info));
	ret = mca_sysfs_create_link_group(SYSFS_DEV_5, "path_control",
					  &pdev->dev, &dada_path_group);
	if (ret)
		goto err_mutex;

	WRITE_ONCE(g_path, info);
	INIT_DELAYED_WORK(&info->init_work, dada_path_init_work);
	schedule_delayed_work(&info->init_work,
			      msecs_to_jiffies(DADA_PATH_INIT_DELAY_MS));
	mca_log_info("path-control ready routes=%d cp_role=%d boost=%d/%d\n",
		     info->route_count, info->cp_role,
		     info->otg_boost_src, info->rev_boost_src);
	return 0;

err_mutex:
	mutex_destroy(&info->lock);
	return dev_err_probe(&pdev->dev, ret, "failed to initialize path-control\n");
}

static int dada_path_remove(struct platform_device *pdev)
{
	struct dada_path_control *info = platform_get_drvdata(pdev);

	if (!info)
		return 0;
	if (READ_ONCE(g_path) == info)
		WRITE_ONCE(g_path, NULL);
	cancel_delayed_work_sync(&info->init_work);
	mca_sysfs_remove_link_group(SYSFS_DEV_5, "path_control", &pdev->dev,
				    &dada_path_group);
	mutex_destroy(&info->lock);
	return 0;
}

static const struct of_device_id dada_path_match[] = {
	{ .compatible = "mca,path_control" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_path_match);

static struct platform_driver dada_path_driver = {
	.driver = {
		.name = "mca_path_control",
		.of_match_table = dada_path_match,
	},
	.probe = dada_path_probe,
	.remove = dada_path_remove,
};
module_platform_driver(dada_path_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA charging path controller");
MODULE_LICENSE("GPL v2");
