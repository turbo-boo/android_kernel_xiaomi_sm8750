// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA adapter protocol class dispatcher for Dada. */
#include <linux/slab.h>
#include <linux/mutex.h>
#include <mca/common/mca_callback.h>

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <mca/protocol/protocol_class.h>

struct mca_protocol_data {
	struct adapter_protocol_class_ops *ops;
	void *data;
};

static struct mca_protocol_data *protocol_data[ADAPTER_PROTOCOL_MAX];
static DEFINE_MUTEX(mca_provider_registry_lock);
DEFINE_STATIC_SRCU(mca_provider_callbacks);

static struct mca_protocol_data *mca_protocol_get(unsigned int protocol)
{
	if (protocol >= ADAPTER_PROTOCOL_MAX)
		return NULL;
	return srcu_dereference(protocol_data[protocol], &mca_provider_callbacks);
}

int protocol_class_register_ops(unsigned int protocol, struct adapter_protocol_class_ops *ops, void *data)
{
	struct mca_protocol_data *entry;
	int ret = 0;

	if (protocol >= ADAPTER_PROTOCOL_MAX || !ops)
		return -EINVAL;
	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->ops = ops;
	entry->data = data;
	mutex_lock(&mca_provider_registry_lock);
	if (protocol_data[protocol])
		ret = -EBUSY;
	else
		rcu_assign_pointer(protocol_data[protocol], entry);
	mutex_unlock(&mca_provider_registry_lock);
	if (ret)
		kfree(entry);
	return ret;
}
EXPORT_SYMBOL(protocol_class_register_ops);

void protocol_class_unregister_ops(unsigned int protocol, void *data)
{
	struct mca_protocol_data *entry = NULL;

	if (protocol >= ADAPTER_PROTOCOL_MAX)
		return;
	mutex_lock(&mca_provider_registry_lock);
	if (protocol_data[protocol] && protocol_data[protocol]->data == data) {
		entry = protocol_data[protocol];
		rcu_assign_pointer(protocol_data[protocol], NULL);
	}
	mutex_unlock(&mca_provider_registry_lock);
	if (entry) {
		synchronize_srcu(&mca_provider_callbacks);
		kfree(entry);
	}
}
EXPORT_SYMBOL(protocol_class_unregister_ops);


int protocol_class_set_adapter_verified(unsigned int protocol, int verified)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!d || !d->ops->set_adapter_verified)
		return -EOPNOTSUPP;
	return d->ops->set_adapter_verified(d->data, verified);
}
EXPORT_SYMBOL(protocol_class_set_adapter_verified);

int protocol_class_get_adapter_verified(unsigned int protocol, int *verified)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!verified)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_verified)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_verified(d->data, verified);
}
EXPORT_SYMBOL(protocol_class_get_adapter_verified);

int protocol_class_get_adapter_max_power(unsigned int protocol,
					 unsigned int *max_power)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!max_power)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_max_power)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_max_power(d->data, max_power);
}
EXPORT_SYMBOL(protocol_class_get_adapter_max_power);

int protocol_class_get_adapter_pwr_max_power(unsigned int protocol,
					     unsigned int *max_power)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!max_power)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_pwr_max_power)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_pwr_max_power(d->data, max_power);
}
EXPORT_SYMBOL(protocol_class_get_adapter_pwr_max_power);

int protocol_class_det_adapter_type(unsigned int protocol, int en)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!d || !d->ops->adapter_det_en)
		return -EOPNOTSUPP;
	return d->ops->adapter_det_en(d->data, en);
}
EXPORT_SYMBOL(protocol_class_det_adapter_type);

int protocol_class_get_adapter_type(unsigned int protocol, unsigned int *value)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!value)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_type)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_type(d->data, (int *)value);
}
EXPORT_SYMBOL(protocol_class_get_adapter_type);

int protocol_class_get_adapter_power_cap(unsigned int protocol,
					 struct adapter_power_cap_info *cap)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!cap)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_pwr_cap)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_pwr_cap(d->data, cap);
}
EXPORT_SYMBOL(protocol_class_get_adapter_power_cap);

int protocol_class_set_adapter_volt_and_curr(unsigned int protocol,
					     int volt, int curr)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!d || !d->ops->set_adapter_volt_and_curr)
		return -EOPNOTSUPP;
	return d->ops->set_adapter_volt_and_curr(d->data, volt, curr);
}
EXPORT_SYMBOL(protocol_class_set_adapter_volt_and_curr);

int protocol_class_get_adapter_volt_and_curr(unsigned int protocol,
					     int *volt, int *curr)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!volt || !curr)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_volt_and_curr)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_volt_and_curr(d->data, volt, curr);
}
EXPORT_SYMBOL(protocol_class_get_adapter_volt_and_curr);

int protocol_class_get_adapter_pps_ptf(unsigned int protocol, int *pps_ptf)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!pps_ptf)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_pps_ptf)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_pps_ptf(d->data, pps_ptf);
}
EXPORT_SYMBOL(protocol_class_get_adapter_pps_ptf);

int protocol_class_get_adapter_info(unsigned int protocol,
				    struct adapter_vendor_info *info)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!info)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_info)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_info(d->data, info);
}
EXPORT_SYMBOL(protocol_class_get_adapter_info);

int protocol_class_get_adapter_power_curve(unsigned int protocol,
					   struct adapter_power_curve *curve)
{
	CLASS(mca_callback, callback_scope)(&mca_provider_callbacks);

	struct mca_protocol_data *d = mca_protocol_get(protocol);
	if (!curve)
		return -EINVAL;
	if (!d || !d->ops->get_adapter_power_curve)
		return -EOPNOTSUPP;
	return d->ops->get_adapter_power_curve(d->data, curve);
}
EXPORT_SYMBOL(protocol_class_get_adapter_power_curve);

static int mca_protocol_probe(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id mca_protocol_match[] = {
	{ .compatible = "mca,protocol_core" },
	{},
};
MODULE_DEVICE_TABLE(of, mca_protocol_match);

static struct platform_driver mca_protocol_driver = {
	.driver = {
		.name = "protocol_class",
		.of_match_table = mca_protocol_match,
	},
	.probe = mca_protocol_probe,
};
module_platform_driver(mca_protocol_driver);

MODULE_DESCRIPTION("Xiaomi MCA adapter protocol class dispatcher");
MODULE_LICENSE("GPL v2");
