// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <mca/common/mca_sysfs.h>

#define MCA_DBG_ROOT_DIR "charger_debug"

struct mca_class_node {
	char *name;
	struct class *class;
	struct list_head devices;
	struct list_head node;
};

struct mca_device_node {
	char *name;
	struct device *dev;
	struct list_head node;
};

static LIST_HEAD(mca_classes);
static DEFINE_MUTEX(mca_sysfs_lock);
static struct dentry *mca_debug_root;

static struct mca_class_node *mca_find_class(const char *name)
{
	struct mca_class_node *n;

	list_for_each_entry(n, &mca_classes, node)
		if (!strcmp(n->name, name))
			return n;
	return NULL;
}

static struct mca_class_node *mca_get_class(const char *name)
{
	struct mca_class_node *n;

	mutex_lock(&mca_sysfs_lock);
	n = mca_find_class(name);
	if (n)
		goto out;

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		goto out;
	n->name = kstrdup(name, GFP_KERNEL);
	if (!n->name)
		goto err_free;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
	n->class = class_create(THIS_MODULE, name);
#else
	n->class = class_create(name);
#endif
	if (IS_ERR(n->class)) {
		n->class = NULL;
		goto err_name;
	}
	INIT_LIST_HEAD(&n->devices);
	list_add_tail(&n->node, &mca_classes);
	goto out;

err_name:
	kfree(n->name);
err_free:
	kfree(n);
	n = NULL;
out:
	mutex_unlock(&mca_sysfs_lock);
	return n;
}

static struct mca_device_node *mca_find_device(struct mca_class_node *cls,
					       const char *name)
{
	struct mca_device_node *n;

	list_for_each_entry(n, &cls->devices, node)
		if (!strcmp(n->name, name))
			return n;
	return NULL;
}

static struct mca_device_node *mca_get_device(struct mca_class_node *cls,
					      const char *name)
{
	struct mca_device_node *n;

	mutex_lock(&mca_sysfs_lock);
	n = mca_find_device(cls, name);
	if (n)
		goto out;

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		goto out;
	n->name = kstrdup(name, GFP_KERNEL);
	if (!n->name)
		goto err_free;
	n->dev = device_create(cls->class, NULL, 0, NULL, "%s", name);
	if (IS_ERR(n->dev)) {
		n->dev = NULL;
		goto err_name;
	}
	list_add_tail(&n->node, &cls->devices);
	goto out;

err_name:
	kfree(n->name);
err_free:
	kfree(n);
	n = NULL;
out:
	mutex_unlock(&mca_sysfs_lock);
	return n;
}

struct device *mca_sysfs_create_group(const char *cls_name,
				      const char *dev_name,
				      const struct attribute_group *group)
{
	struct mca_class_node *cls;
	struct mca_device_node *devn;

	if (!cls_name || !dev_name || !group)
		return NULL;
	cls = mca_get_class(cls_name);
	if (!cls)
		return NULL;
	devn = mca_get_device(cls, dev_name);
	if (!devn)
		return NULL;
	if (sysfs_create_group(&devn->dev->kobj, group))
		return NULL;
	return devn->dev;
}
EXPORT_SYMBOL(mca_sysfs_create_group);

void mca_sysfs_remove_group(const char *cls_name, struct device *dev,
			    const struct attribute_group *group)
{
	if (dev && group)
		sysfs_remove_group(&dev->kobj, group);
}
EXPORT_SYMBOL(mca_sysfs_remove_group);

int mca_sysfs_create_link_group(const char *dev_name, const char *link_name,
				struct device *target_dev,
				const struct attribute_group *group)
{
	struct mca_class_node *cls;
	struct mca_device_node *devn;
	int ret;

	if (!dev_name || !link_name || !target_dev || !group)
		return -EINVAL;
	mutex_lock(&mca_sysfs_lock);
	cls = mca_find_class("xm_power");
	devn = cls ? mca_find_device(cls, dev_name) : NULL;
	mutex_unlock(&mca_sysfs_lock);
	if (!devn)
		return -ENODEV;
	ret = sysfs_create_group(&target_dev->kobj, group);
	if (ret)
		return ret;
	ret = sysfs_create_link(&devn->dev->kobj, &target_dev->kobj, link_name);
	if (ret)
		sysfs_remove_group(&target_dev->kobj, group);
	return ret;
}
EXPORT_SYMBOL(mca_sysfs_create_link_group);

void mca_sysfs_remove_link_group(const char *dev_name, const char *link_name,
				 struct device *target_dev,
				 const struct attribute_group *group)
{
	struct mca_class_node *cls;
	struct mca_device_node *devn;

	if (!dev_name || !link_name || !target_dev || !group)
		return;
	mutex_lock(&mca_sysfs_lock);
	cls = mca_find_class("xm_power");
	devn = cls ? mca_find_device(cls, dev_name) : NULL;
	mutex_unlock(&mca_sysfs_lock);
	if (!devn)
		return;
	sysfs_remove_link(&devn->dev->kobj, link_name);
	sysfs_remove_group(&target_dev->kobj, group);
}
EXPORT_SYMBOL(mca_sysfs_remove_link_group);

int mca_sysfs_create_files(const char *dev_name,
			   struct mca_sysfs_attr_info *attr, const int attr_size)
{
	struct mca_class_node *cls;
	struct mca_device_node *devn;
	int i, ret = 0;

	if (!dev_name || !attr || attr_size < 0)
		return -EINVAL;
	mutex_lock(&mca_sysfs_lock);
	cls = mca_find_class("xm_power");
	devn = cls ? mca_find_device(cls, dev_name) : NULL;
	mutex_unlock(&mca_sysfs_lock);
	if (!devn)
		return -ENODEV;
	for (i = 0; i < attr_size; i++) {
		int r = sysfs_create_file(&devn->dev->kobj, &attr[i].attr.attr);
		if (r && !ret)
			ret = r;
	}
	return ret;
}
EXPORT_SYMBOL(mca_sysfs_create_files);

void mca_sysfs_remove_files(const char *dev_name,
 struct mca_sysfs_attr_info *attr, int attr_size)
{
 struct mca_class_node *cls;
 struct mca_device_node *devn;
 int i;
 if (!dev_name || !attr || attr_size < 0)
  return;
 mutex_lock(&mca_sysfs_lock);
 cls = mca_find_class("xm_power");
 devn = cls ? mca_find_device(cls, dev_name) : NULL;
 mutex_unlock(&mca_sysfs_lock);
 if (!devn)
  return;
 for (i = 0; i < attr_size; i++)
  sysfs_remove_file(&devn->dev->kobj, &attr[i].attr.attr);
}
EXPORT_SYMBOL(mca_sysfs_remove_files);

void mca_sysfs_init_attrs(struct attribute **attrs,
			  struct mca_sysfs_attr_info *attr_info, int size)
{
	int i;

	if (!attrs || !attr_info || size < 0)
		return;
	for (i = 0; i < size; i++)
		attrs[i] = &attr_info[i].attr.attr;
	attrs[size] = NULL;
}
EXPORT_SYMBOL(mca_sysfs_init_attrs);

struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(
	const char *name, struct mca_sysfs_attr_info *attr_info, int size)
{
	int i;

	if (!name || !attr_info || size < 0)
		return NULL;
	for (i = 0; i < size; i++)
		if (!strcmp(name, attr_info[i].attr.attr.name))
			return &attr_info[i];
	return NULL;
}
EXPORT_SYMBOL(mca_sysfs_lookup_attr);

#ifdef CONFIG_DEBUG_FS
struct mca_debug_group {
 struct list_head node;
 struct dentry *dir;
 struct mca_debugfs_attr_data *attrs;
 void *data;
};
static LIST_HEAD(mca_debug_groups);
static DEFINE_MUTEX(mca_debug_groups_lock);
#endif

#ifdef CONFIG_DEBUG_FS
static int mca_debug_show(struct seq_file *s, void *unused)
{
	struct mca_debugfs_attr_data *data = s->private;
	char *buf;
	ssize_t len;

	if (!data || !data->attr_info || !data->attr_info->show)
		return -EINVAL;
	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	len = data->attr_info->show(data, buf);
	if (len > 0)
		seq_write(s, buf, min_t(ssize_t, len, PAGE_SIZE));
	kfree(buf);
	return len < 0 ? len : 0;
}

static int mca_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mca_debug_show, inode->i_private);
}

static ssize_t mca_debug_write(struct file *file, const char __user *ubuf,
			       size_t size, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct mca_debugfs_attr_data *data = seq ? seq->private : NULL;
	char *buf;
	ssize_t ret;

	if (!data || !data->attr_info || !data->attr_info->store)
		return -EINVAL;
	if (!size || size >= PAGE_SIZE)
		return -EINVAL;
	buf = memdup_user_nul(ubuf, size);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	ret = data->attr_info->store(data, buf, size);
	kfree(buf);
	return ret;
}

static const struct file_operations mca_debug_fops = {
	.owner = THIS_MODULE,
	.open = mca_debug_open,
	.read = seq_read,
	.write = mca_debug_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

int mca_debugfs_create_group(const char *dir_name,
 struct mca_debugfs_attr_info *attr_info, int attr_size, void *dev_data)
{
#ifdef CONFIG_DEBUG_FS
 struct mca_debug_group *group;
 int i, ret = -ENOMEM;
 if (!dir_name || !attr_info || attr_size <= 0)
  return -EINVAL;
 group = kzalloc(sizeof(*group), GFP_KERNEL);
 if (!group)
  return -ENOMEM;
 group->attrs = kcalloc(attr_size, sizeof(*group->attrs), GFP_KERNEL);
 if (!group->attrs) {
  kfree(group);
  return -ENOMEM;
 }
 mutex_lock(&mca_debug_groups_lock);
 if (!mca_debug_root)
  mca_debug_root = debugfs_create_dir(MCA_DBG_ROOT_DIR, NULL);
 if (IS_ERR_OR_NULL(mca_debug_root))
  goto failed;
 group->dir = debugfs_create_dir(dir_name, mca_debug_root);
 if (IS_ERR_OR_NULL(group->dir))
  goto failed;
 group->data = dev_data;
 for (i = 0; i < attr_size; i++) {
  group->attrs[i].attr_info = &attr_info[i];
  group->attrs[i].private = dev_data;
  debugfs_create_file(attr_info[i].file, attr_info[i].mode,
   group->dir, &group->attrs[i], &mca_debug_fops);
 }
 list_add_tail(&group->node, &mca_debug_groups);
 mutex_unlock(&mca_debug_groups_lock);
 return 0;
failed:
 mutex_unlock(&mca_debug_groups_lock);
 kfree(group->attrs);
 kfree(group);
 return ret;
#else
 return -EOPNOTSUPP;
#endif
}
EXPORT_SYMBOL(mca_debugfs_create_group);

void mca_debugfs_remove_groups(void *dev_data)
{
#ifdef CONFIG_DEBUG_FS
 struct mca_debug_group *group, *next;
 mutex_lock(&mca_debug_groups_lock);
 list_for_each_entry_safe(group, next, &mca_debug_groups, node) {
  if (group->data != dev_data)
   continue;
  list_del(&group->node);
  debugfs_remove_recursive(group->dir);
  kfree(group->attrs);
  kfree(group);
 }
 mutex_unlock(&mca_debug_groups_lock);
#endif
}
EXPORT_SYMBOL(mca_debugfs_remove_groups);

static void mca_sysfs_cleanup(void)
{
	struct mca_class_node *cls, *next_cls;
	struct mca_device_node *devn, *next_dev;
#ifdef CONFIG_DEBUG_FS
	struct mca_debug_group *group, *next_group;

	mutex_lock(&mca_debug_groups_lock);
	list_for_each_entry_safe(group, next_group, &mca_debug_groups, node) {
		list_del(&group->node);
		debugfs_remove_recursive(group->dir);
		kfree(group->attrs);
		kfree(group);
	}
	debugfs_remove_recursive(mca_debug_root);
	mca_debug_root = NULL;
	mutex_unlock(&mca_debug_groups_lock);
#endif
	mutex_lock(&mca_sysfs_lock);
	list_for_each_entry_safe(cls, next_cls, &mca_classes, node) {
		list_for_each_entry_safe(devn, next_dev, &cls->devices, node) {
			list_del(&devn->node);
			device_unregister(devn->dev);
			kfree(devn->name);
			kfree(devn);
		}
		list_del(&cls->node);
		class_destroy(cls->class);
		kfree(cls->name);
		kfree(cls);
	}
	mutex_unlock(&mca_sysfs_lock);
}

static int __init mca_sysfs_init(void)
{
	static const char * const devices[] = {
		SYSFS_DEV_1, SYSFS_DEV_2, SYSFS_DEV_3, SYSFS_DEV_4, SYSFS_DEV_5,
	};
	struct mca_class_node *cls;
	int i;

	cls = mca_get_class("xm_power");
	if (!cls)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(devices); i++) {
		if (!mca_get_device(cls, devices[i])) {
			mca_sysfs_cleanup();
			return -ENOMEM;
		}
	}
	return 0;
}
module_init(mca_sysfs_init);

static void __exit mca_sysfs_exit(void)
{
	mca_sysfs_cleanup();
}
module_exit(mca_sysfs_exit);

MODULE_DESCRIPTION("Xiaomi MCA sysfs/debugfs core");
MODULE_LICENSE("GPL v2");
