// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/srcu.h>
#include <mca/strategy/strategy_class.h>

struct mca_strategy_func_data {
	mca_strategy_func func;
	mca_strategy_get_status get_func;
	mca_strategy_set_config set_config;
	void *data;
};

static struct mca_strategy_func_data strategy_data[STRATEGY_FUNC_TYPE_MAX];
static DEFINE_MUTEX(strategy_lock);
DEFINE_STATIC_SRCU(strategy_srcu);

int mca_strategy_func_get_status(int type, int status, void *value)
{
	struct mca_strategy_func_data data;
	int idx, ret;

	if (type < 0 || type >= STRATEGY_FUNC_TYPE_MAX)
		return -EINVAL;
	idx = srcu_read_lock(&strategy_srcu);
	mutex_lock(&strategy_lock);
	data = strategy_data[type];
	mutex_unlock(&strategy_lock);
	ret = data.get_func ? data.get_func(status, value, data.data) : -EOPNOTSUPP;
	srcu_read_unlock(&strategy_srcu, idx);
	return ret;
}
EXPORT_SYMBOL(mca_strategy_func_get_status);

int mca_strategy_func_process(unsigned int type, int event, int value)
{
	struct mca_strategy_func_data data;
	int idx, ret;

	if (type >= STRATEGY_FUNC_TYPE_MAX)
		return -EINVAL;
	idx = srcu_read_lock(&strategy_srcu);
	mutex_lock(&strategy_lock);
	data = strategy_data[type];
	mutex_unlock(&strategy_lock);
	ret = data.func ? data.func(event, value, data.data) : -EOPNOTSUPP;
	srcu_read_unlock(&strategy_srcu, idx);
	return ret;
}
EXPORT_SYMBOL(mca_strategy_func_process);

int mca_strategy_func_set_config(int type, int config, int value)
{
	struct mca_strategy_func_data data;
	int idx, ret;

	if (type < 0 || type >= STRATEGY_FUNC_TYPE_MAX)
		return -EINVAL;
	idx = srcu_read_lock(&strategy_srcu);
	mutex_lock(&strategy_lock);
	data = strategy_data[type];
	mutex_unlock(&strategy_lock);
	ret = data.set_config ? data.set_config(config, value, data.data) : -EOPNOTSUPP;
	srcu_read_unlock(&strategy_srcu, idx);
	return ret;
}
EXPORT_SYMBOL(mca_strategy_func_set_config);

int mca_strategy_ops_register(unsigned int type, mca_strategy_func func,
			      mca_strategy_get_status get_func,
			      mca_strategy_set_config set_config, void *data)
{
	if (type >= STRATEGY_FUNC_TYPE_MAX)
		return -EINVAL;
	mutex_lock(&strategy_lock);
	strategy_data[type].func = func;
	strategy_data[type].get_func = get_func;
	strategy_data[type].set_config = set_config;
	strategy_data[type].data = data;
	mutex_unlock(&strategy_lock);
	return 0;
}
EXPORT_SYMBOL(mca_strategy_ops_register);

void mca_strategy_ops_unregister(unsigned int type, void *data)
{
	if (type >= STRATEGY_FUNC_TYPE_MAX)
		return;
	mutex_lock(&strategy_lock);
	if (strategy_data[type].data == data)
		memset(&strategy_data[type], 0, sizeof(strategy_data[type]));
	mutex_unlock(&strategy_lock);
	/* Callbacks may sleep or recurse into another strategy. */
	synchronize_srcu(&strategy_srcu);
}
EXPORT_SYMBOL(mca_strategy_ops_unregister);

MODULE_DESCRIPTION("Xiaomi MCA strategy registry");
MODULE_LICENSE("GPL v2");
