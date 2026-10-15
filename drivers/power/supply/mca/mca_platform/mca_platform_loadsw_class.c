// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA load-switch platform dispatcher for Dada. */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_loadsw_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "loadsw_class"
#endif

#define MCA_LOADSW_MAX_NUM LOADSW_ROLE_MAX

struct loadsw_ops_data {
	struct platform_class_loadsw_ops *ops;
	void *data;
};

struct platform_loadsw_dev {
	struct device *dev;
	int loadsw_num;
	const char *dir[LOADSW_ROLE_MAX];
	struct device *sysfs_dev[LOADSW_ROLE_MAX];
	int index[LOADSW_ROLE_MAX];
};

static struct loadsw_ops_data loadsw_data[LOADSW_ROLE_MAX];

static struct loadsw_ops_data *loadsw_get(unsigned int role)
{
	if (role >= LOADSW_ROLE_MAX || !loadsw_data[role].ops)
		return NULL;
	return &loadsw_data[role];
}

int platform_class_loadsw_register_ops(unsigned int role,
				       struct platform_class_loadsw_ops *ops,
				       void *data)
{
	if (role >= LOADSW_ROLE_MAX || !ops || !data)
		return -EINVAL;
	loadsw_data[role].ops = ops;
	loadsw_data[role].data = data;
	return 0;
}
EXPORT_SYMBOL(platform_class_loadsw_register_ops);

int platform_class_loadsw_get_present(unsigned int role, bool *present)
{
	struct loadsw_ops_data *d = loadsw_get(role);

	if (!present)
		return -EINVAL;
	if (!d || !d->ops->loadsw_get_present)
		return -EOPNOTSUPP;
	return d->ops->loadsw_get_present(present, d->data);
}
EXPORT_SYMBOL(platform_class_loadsw_get_present);

int platform_class_loadsw_get_ibat_limit(unsigned int role, int *limit)
{
	struct loadsw_ops_data *d = loadsw_get(role);

	if (!limit)
		return -EINVAL;
	if (!d || !d->ops->loadsw_get_ibat_limit)
		return -EOPNOTSUPP;
	return d->ops->loadsw_get_ibat_limit(limit, d->data);
}
EXPORT_SYMBOL(platform_class_loadsw_get_ibat_limit);

int platform_class_loadsw_set_ibat_limit(unsigned int role, int value)
{
	struct loadsw_ops_data *d = loadsw_get(role);

	if (!d || !d->ops->loadsw_set_ibat_limit)
		return -EOPNOTSUPP;
	return d->ops->loadsw_set_ibat_limit(value, d->data);
}
EXPORT_SYMBOL(platform_class_loadsw_set_ibat_limit);

int platform_class_loadsw_set_lowpower_mode(unsigned int role, bool mode)
{
	struct loadsw_ops_data *d = loadsw_get(role);

	if (!d || !d->ops->loadsw_set_lowpower_mode)
		return -EOPNOTSUPP;
	return d->ops->loadsw_set_lowpower_mode(mode, d->data);
}
EXPORT_SYMBOL(platform_class_loadsw_set_lowpower_mode);

int platform_class_loadsw_get_lowpower_mode(unsigned int role, bool *mode)
{
	struct loadsw_ops_data *d = loadsw_get(role);

	if (!mode)
		return -EINVAL;
	if (!d || !d->ops->loadsw_get_lowpower_mode)
		return -EOPNOTSUPP;
	return d->ops->loadsw_get_lowpower_mode(mode, d->data);
}
EXPORT_SYMBOL(platform_class_loadsw_get_lowpower_mode);

enum loadsw_attr {
	LOADSW_ATTR_CHIP_OK = 0,
	LOADSW_ATTR_IBAT_LIMIT,
	LOADSW_ATTR_LOW_POWER,
};

static ssize_t loadsw_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int *index = dev_get_drvdata(dev);
	bool b = false;
	int value = 0, ret;

	if (!index || *index < 0 || *index >= LOADSW_ROLE_MAX)
		return -ENODEV;

	if (!strcmp(attr->attr.name, "chip_ok")) {
		ret = platform_class_loadsw_get_present(*index, &b);
		return ret ? ret : sysfs_emit(buf, "%d\n", b);
	}
	if (!strcmp(attr->attr.name, "ibat_limit")) {
		ret = platform_class_loadsw_get_ibat_limit(*index, &value);
		return ret ? ret : sysfs_emit(buf, "%d\n", value);
	}
	if (!strcmp(attr->attr.name, "low_power")) {
		ret = platform_class_loadsw_get_lowpower_mode(*index, &b);
		return ret ? ret : sysfs_emit(buf, "%d\n", b);
	}
	return -EINVAL;
}

static ssize_t loadsw_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int *index = dev_get_drvdata(dev);
	int value, ret;

	if (!index || *index < 0 || *index >= LOADSW_ROLE_MAX)
		return -ENODEV;
	if (kstrtoint(buf, 10, &value))
		return -EINVAL;

	if (!strcmp(attr->attr.name, "ibat_limit"))
		ret = platform_class_loadsw_set_ibat_limit(*index, value);
	else if (!strcmp(attr->attr.name, "low_power"))
		ret = platform_class_loadsw_set_lowpower_mode(*index, !!value);
	else
		return -EACCES;
	return ret ? ret : count;
}

static struct mca_sysfs_attr_info loadsw_attrs_info[] = {
	mca_sysfs_attr_ro(loadsw, 0440, LOADSW_ATTR_CHIP_OK, chip_ok),
	mca_sysfs_attr_rw(loadsw, 0664, LOADSW_ATTR_IBAT_LIMIT, ibat_limit),
	mca_sysfs_attr_rw(loadsw, 0664, LOADSW_ATTR_LOW_POWER, low_power),
};
static struct attribute *loadsw_attrs[ARRAY_SIZE(loadsw_attrs_info) + 1];
static const struct attribute_group loadsw_group = { .attrs = loadsw_attrs };

static int platform_loadsw_parse_dt(struct platform_loadsw_dev *info)
{
	struct device_node *np = info->dev->of_node;
	int count, i, ret;

	if (!np)
		return -ENODEV;
	ret = mca_parse_dts_u32(np, "loadsw-num", &info->loadsw_num, 1);
	if (ret)
		return ret;
	if (info->loadsw_num < 1 || info->loadsw_num > MCA_LOADSW_MAX_NUM)
		return -EINVAL;

	count = mca_parse_dts_count_strings(np, "loadsw-dir-list",
					    MCA_LOADSW_MAX_NUM,
					    info->loadsw_num);
	if (count != info->loadsw_num)
		return -EINVAL;
	for (i = 0; i < count; i++) {
		ret = mca_parse_dts_string_index(np, "loadsw-dir-list", i,
						 &info->dir[i]);
		if (ret)
			return ret;
	}
	return 0;
}

static int platform_loadsw_probe(struct platform_device *pdev)
{
	struct platform_loadsw_dev *info;
	int i, ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	ret = platform_loadsw_parse_dt(info);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to parse load-switch DT\n");

	mca_sysfs_init_attrs(loadsw_attrs, loadsw_attrs_info,
			     ARRAY_SIZE(loadsw_attrs_info));
	for (i = 0; i < info->loadsw_num; i++) {
		info->index[i] = i;
		info->sysfs_dev[i] = mca_sysfs_create_group("xm_power",
							     info->dir[i],
							     &loadsw_group);
		if (!info->sysfs_dev[i])
			return -ENOMEM;
		dev_set_drvdata(info->sysfs_dev[i], &info->index[i]);
	}

	mca_log_info("load-switch platform class ready count=%d\n",
		     info->loadsw_num);
	return 0;
}

static int platform_loadsw_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id platform_loadsw_match[] = {
	{ .compatible = "mca,platform_loadsw" },
	{},
};
MODULE_DEVICE_TABLE(of, platform_loadsw_match);

static struct platform_driver platform_loadsw_driver = {
	.driver = {
		.name = "platform_loadsw_class",
		.of_match_table = platform_loadsw_match,
	},
	.probe = platform_loadsw_probe,
	.remove = platform_loadsw_remove,
};
module_platform_driver(platform_loadsw_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA load-switch platform class");
MODULE_LICENSE("GPL v2");
