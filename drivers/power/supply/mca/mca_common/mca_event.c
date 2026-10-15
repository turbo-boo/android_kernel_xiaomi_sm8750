// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_sysfs.h>

struct mca_event_dev {
	struct device *dev;
};

static struct mca_event_dev *mca_event_dev;
static struct blocking_notifier_head mca_event_heads[MCA_EVENT_TYPE_END];
static struct attribute *mca_event_attrs[] = { NULL };
static const struct attribute_group mca_event_group = {
	.attrs = mca_event_attrs,
};

int mca_event_block_notify_register(unsigned int type, struct notifier_block *nb)
{
	if (!mca_event_dev)
		return -ENODEV;
	if (type >= MCA_EVENT_TYPE_END || !nb)
		return -EINVAL;
	return blocking_notifier_chain_register(&mca_event_heads[type], nb);
}
EXPORT_SYMBOL(mca_event_block_notify_register);

int mca_event_block_notify_unregister(unsigned int type, struct notifier_block *nb)
{
	if (!mca_event_dev)
		return -ENODEV;
	if (type >= MCA_EVENT_TYPE_END || !nb)
		return -EINVAL;
	return blocking_notifier_chain_unregister(&mca_event_heads[type], nb);
}
EXPORT_SYMBOL(mca_event_block_notify_unregister);

void mca_event_block_notify(unsigned int type, unsigned long event, void *data)
{
	if (!mca_event_dev || type >= MCA_EVENT_TYPE_END)
		return;
	blocking_notifier_call_chain(&mca_event_heads[type], event, data);
}
EXPORT_SYMBOL(mca_event_block_notify);

void mca_event_report_uevent(const struct mca_event_notify_data *n_data)
{
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	char *envp[] = { event, NULL };
	int len;

	if (!mca_event_dev || !mca_event_dev->dev || !n_data || !n_data->event)
		return;
	len = min_t(int, n_data->event_len, MCA_EVENT_NOTIFY_SIZE - 1);
	if (len <= 0)
		return;
	memcpy(event, n_data->event, len);
	kobject_uevent_env(&mca_event_dev->dev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(mca_event_report_uevent);

void mca_event_report_multiple_uevent(const struct mca_event_notify_data *n_data,
				      unsigned int num)
{
	char **envp;
	char *storage;
	unsigned int i;

	if (!mca_event_dev || !mca_event_dev->dev || !n_data || !num)
		return;
	storage = kcalloc(num, MCA_EVENT_NOTIFY_SIZE, GFP_KERNEL);
	if (!storage)
		return;
	envp = kcalloc(num + 1, sizeof(*envp), GFP_KERNEL);
	if (!envp) {
		kfree(storage);
		return;
	}
	for (i = 0; i < num; i++) {
		int len;
		if (!n_data[i].event)
			continue;
		len = min_t(int, n_data[i].event_len, MCA_EVENT_NOTIFY_SIZE - 1);
		if (len > 0)
			memcpy(storage + i * MCA_EVENT_NOTIFY_SIZE,
			       n_data[i].event, len);
		envp[i] = storage + i * MCA_EVENT_NOTIFY_SIZE;
	}
	kobject_uevent_env(&mca_event_dev->dev->kobj, KOBJ_CHANGE, envp);
	kfree(envp);
	kfree(storage);
}
EXPORT_SYMBOL(mca_event_report_multiple_uevent);

static int __init mca_event_init(void)
{
	int i;

	mca_event_dev = kzalloc(sizeof(*mca_event_dev), GFP_KERNEL);
	if (!mca_event_dev)
		return -ENOMEM;
	for (i = 0; i < MCA_EVENT_TYPE_END; i++)
		BLOCKING_INIT_NOTIFIER_HEAD(&mca_event_heads[i]);
	mca_event_dev->dev = mca_sysfs_create_group("xm_power", "mca_event",
						 &mca_event_group);
	/* Notifier functionality remains valid if sysfs creation is unavailable. */
	return 0;
}
module_init(mca_event_init);

static void __exit mca_event_exit(void)
{
	mca_sysfs_remove_group("xm_power", mca_event_dev->dev,
			       &mca_event_group);
	kfree(mca_event_dev);
	mca_event_dev = NULL;
}
module_exit(mca_event_exit);

MODULE_DESCRIPTION("Xiaomi MCA event notifier core");
MODULE_LICENSE("GPL v2");
