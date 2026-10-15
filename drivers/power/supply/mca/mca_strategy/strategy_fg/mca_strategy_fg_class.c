// SPDX-License-Identifier: GPL-2.0
/*
 *mca_strategy_fg_class.c
 *
 * mca fuelgauge strategy class driver
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#include <linux/slab.h>
#include <linux/mutex.h>
#include <mca/common/mca_callback.h>

#include <linux/module.h>
#include <linux/platform_device.h>
#include <mca/strategy/strategy_fg_class.h>

static struct strategy_fg_class_info *g_stg_fg_class_info;
static DEFINE_MUTEX(mca_fg_registry_lock);
DEFINE_STATIC_SRCU(mca_fg_callbacks);

#define is_invalid_ops(name) \
	(!binding || !binding->ops || !(binding->ops->name))
#define strategy_fg_class_ops_no_para(name) \
	(binding->ops->name(binding->data))
#define strategy_fg_class_ops_with_one_para(name, para1) \
	(binding->ops->name(binding->data, para1))

int strategy_class_fg_ops_register(void *data, struct strategy_fg_class_ops *ops)
{
	struct strategy_fg_class_info *entry;
	int ret = 0;

	if (!data || !ops)
		return -EINVAL;
	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->data = data;
	entry->ops = ops;
	mutex_lock(&mca_fg_registry_lock);
	if (g_stg_fg_class_info)
		ret = -EBUSY;
	else
		rcu_assign_pointer(g_stg_fg_class_info, entry);
	mutex_unlock(&mca_fg_registry_lock);
	if (ret)
		kfree(entry);
	return ret;
}
EXPORT_SYMBOL(strategy_class_fg_ops_register);

void strategy_class_fg_ops_unregister(void *data)
{
	struct strategy_fg_class_info *entry = NULL;

	mutex_lock(&mca_fg_registry_lock);
	if (g_stg_fg_class_info && g_stg_fg_class_info->data == data) {
		entry = g_stg_fg_class_info;
		rcu_assign_pointer(g_stg_fg_class_info, NULL);
	}
	mutex_unlock(&mca_fg_registry_lock);
	if (entry) {
		synchronize_srcu(&mca_fg_callbacks);
		kfree(entry);
	}
}
EXPORT_SYMBOL(strategy_class_fg_ops_unregister);


int strategy_class_fg_ops_is_init_ok(void)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_is_init_ok))
		return -1;

	return strategy_fg_class_ops_no_para(strategy_fg_is_init_ok);
}
EXPORT_SYMBOL(strategy_class_fg_ops_is_init_ok);

int strategy_class_fg_ops_get_rsoc(int *rsoc)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_rsoc))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_rsoc, rsoc);
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_rsoc);

int strategy_class_fg_ops_get_soc(void)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_soc))
		return -1;

	return strategy_fg_class_ops_no_para(strategy_fg_get_soc);
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_soc);

int strategy_class_fg_ops_get_temperature(int *temp)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_temp))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_temp, temp);
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_temperature);

int strategy_class_fg_ops_get_current(int *curr)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_current))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_current,
						   curr);
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_current);

int strategy_class_fg_ops_get_voltage(int *volt)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_voltage))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_voltage,
						   volt);
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_voltage);

int strategy_class_fg_ops_get_cyclecount(int *cycle)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_cycle))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_cycle,
						   cycle);
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_cyclecount);

int strategy_class_fg_get_voltage_mean(int *vol_mean)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_voltage_mean))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_voltage_mean,
						   vol_mean);
}
EXPORT_SYMBOL(strategy_class_fg_get_voltage_mean);

int strategy_class_fg_ops_get_soc_decimal(int *soc_decimal, int *rate)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_soc_decimal_info))
		return -1;

	return binding->ops->strategy_fg_get_soc_decimal_info(
		binding->data, soc_decimal, rate);
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_soc_decimal);

bool strategy_class_fg_ops_get_charging_done(void)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_charging_done))
		return -1;

	return strategy_fg_class_ops_no_para(strategy_fg_get_charging_done);
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_charging_done);

int strategy_class_fg_ops_set_charging_done(bool charging_done)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_set_charging_done))
		return -1;

	return strategy_fg_class_ops_with_one_para(
		strategy_fg_set_charging_done, charging_done);
}
EXPORT_SYMBOL(strategy_class_fg_ops_set_charging_done);

int strategy_class_fg_get_model_name(const char **model_name)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_model_name))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_model_name,
						   model_name);
}
EXPORT_SYMBOL(strategy_class_fg_get_model_name);

int strategy_class_fg_set_fastcharge(bool en)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_set_fastcharge))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_set_fastcharge,
						   en);
}
EXPORT_SYMBOL(strategy_class_fg_set_fastcharge);

int strategy_class_fg_get_fastcharge(void)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_fastcharge))
		return -1;

	return strategy_fg_class_ops_no_para(strategy_fg_get_fastcharge);
}
EXPORT_SYMBOL(strategy_class_fg_get_fastcharge);

int strategy_class_fg_get_authentic(bool *authentic)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_authentic))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_authentic,
						   authentic);
}
EXPORT_SYMBOL(strategy_class_fg_get_authentic);

int strategy_class_fg_get_dc(int *dc)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_dc))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_dc, dc);
}
EXPORT_SYMBOL(strategy_class_fg_get_dc);

int strategy_class_fg_get_rm(int *rm)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_rm))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_rm, rm);
}
EXPORT_SYMBOL(strategy_class_fg_get_rm);

int strategy_class_fg_get_fcc(int *fcc)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_fcc))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_fcc, fcc);
}
EXPORT_SYMBOL(strategy_class_fg_get_fcc);

int strategy_class_fg_is_chip_ok(void)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_is_chip_ok))
		return -1;

	return strategy_fg_class_ops_no_para(strategy_fg_is_chip_ok);
}
EXPORT_SYMBOL(strategy_class_fg_is_chip_ok);

int strategy_class_fg_get_health(int *health)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_health))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_health,
						   health);
}
EXPORT_SYMBOL(strategy_class_fg_get_health);

int strategy_class_fg_get_first_termination(int *value)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_first_termination))
		return -1;

	return strategy_fg_class_ops_with_one_para(
		strategy_fg_get_first_termination, value);
}
EXPORT_SYMBOL(strategy_class_fg_get_first_termination);

int strategy_class_fg_get_pack_vendor_id(int *vendor_id)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_pack_vendor_id))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_pack_vendor_id, vendor_id);
}
EXPORT_SYMBOL(strategy_class_fg_get_pack_vendor_id);

int strategy_class_fg_dual_is_chip_ok(int index)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_dual_is_chip_ok))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_dual_is_chip_ok, index);
}
EXPORT_SYMBOL(strategy_class_fg_dual_is_chip_ok);

int strategy_class_fg_ops_get_thermal_temperature(int *temp)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_thermal_temperature))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_thermal_temperature, temp);
}
EXPORT_SYMBOL(strategy_class_fg_ops_get_thermal_temperature);

int strategy_class_fg_get_soh(int *soh)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_soh))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_soh, soh);
}
EXPORT_SYMBOL(strategy_class_fg_get_soh);

int strategy_class_fg_get_temp_offset_flag(int *flag)
{
	CLASS(mca_callback, callback_scope)(&mca_fg_callbacks);
	struct strategy_fg_class_info *binding = srcu_dereference(g_stg_fg_class_info, &mca_fg_callbacks);

	if (is_invalid_ops(strategy_fg_get_temp_offset_flag))
		return -1;

	return strategy_fg_class_ops_with_one_para(strategy_fg_get_temp_offset_flag,
						   flag);
}
EXPORT_SYMBOL(strategy_class_fg_get_temp_offset_flag);

static struct platform_driver strategy_fg_class_driver = {
	.driver	= {
		.name = "strategy_fg_class",
	},
};

module_platform_driver(strategy_fg_class_driver);
MODULE_DESCRIPTION("platform fg class");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
