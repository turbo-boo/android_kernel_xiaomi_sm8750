#ifndef _MCA_COMMON_MCA_SYSFS_H_
#define _MCA_COMMON_MCA_SYSFS_H_

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define SYSFS_DEV_1 "charger"
#define SYSFS_DEV_2 "fuelgauge"
#define SYSFS_DEV_3 "typec"
#define SYSFS_DEV_4 "battery"
#define SYSFS_DEV_5 "hw_monitor"

struct mca_sysfs_attr_info {
	struct device_attribute attr;
	int sysfs_attr_name;
};

#define mca_sysfs_attr_ro(_prefix, _mode, _name, _file) \
	{ .attr = __ATTR(_file, _mode, _prefix##_show, NULL), \
	  .sysfs_attr_name = (_name) }
#define mca_sysfs_attr_wo(_prefix, _mode, _name, _file) \
	{ .attr = __ATTR(_file, _mode, NULL, _prefix##_store), \
	  .sysfs_attr_name = (_name) }
#define mca_sysfs_attr_rw(_prefix, _mode, _name, _file) \
	{ .attr = __ATTR(_file, _mode, _prefix##_show, _prefix##_store), \
	  .sysfs_attr_name = (_name) }

struct mca_debugfs_attr_info {
	const char *file;
	umode_t mode;
	int debugfs_attr_name;
	ssize_t (*show)(void *priv_data, char *buf);
	ssize_t (*store)(void *priv_data, const char *buf, size_t count);
};

struct mca_debugfs_attr_data {
	struct mca_debugfs_attr_info *attr_info;
	void *private;
};

#define mca_debugfs_attr(_prefix, _mode, _name, _file) \
	{ .file = #_file, .mode = (_mode), .debugfs_attr_name = (_name), \
	  .show = _prefix##_show, .store = _prefix##_store }

struct device *mca_sysfs_create_group(const char *cls_name,
				      const char *dev_name,
				      const struct attribute_group *group);
void mca_sysfs_remove_group(const char *cls_name, struct device *dev,
			    const struct attribute_group *group);
int mca_sysfs_create_link_group(const char *dev_name, const char *link_name,
				struct device *target_dev,
				const struct attribute_group *group);
void mca_sysfs_remove_link_group(const char *dev_name, const char *link_name,
				 struct device *target_dev,
				 const struct attribute_group *group);
int mca_sysfs_create_files(const char *dev_name,
			   struct mca_sysfs_attr_info *attr, const int attr_size);
void mca_sysfs_remove_files(const char *dev_name,
			   struct mca_sysfs_attr_info *attr, int attr_size);
void mca_sysfs_init_attrs(struct attribute **attrs,
			  struct mca_sysfs_attr_info *attr_info, int size);
struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(
	const char *name, struct mca_sysfs_attr_info *attr_info, int size);
int mca_debugfs_create_group(const char *dir_name,
			     struct mca_debugfs_attr_info *attr_info,
			     int attr_size, void *dev_data);
void mca_debugfs_remove_groups(void *dev_data);

#endif
