// SPDX-License-Identifier: GPL-2.0
/*
 * mca_charger_wireless_psy.c
 *
 * mca_charger_wireless_psy
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <mca/common/mca_log.h>
#include <mca/strategy/strategy_class.h>
#include "inc/mca_charger_wireless_psy.h"
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_wireless_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_charger_wireless_psy"
#endif

static bool is_batt_available(struct wireless_psy_info *info)
{
	if (!info->batt_psy)
		info->batt_psy = power_supply_get_by_name("battery");

	if (!info->batt_psy)
		return false;

	return true;
}

static enum power_supply_property wireless_psy_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
};

static int wireless_psy_get_prop(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	int rc = 0;
	struct wireless_psy_info *info = power_supply_get_drvdata(psy);

	if (!is_batt_available(info)) {
		mca_log_err("get batt psy fail\n");
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_ONLINE, &info->present);
		val->intval = info->present;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = info->online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		strategy_class_fg_ops_get_voltage(&info->vbat);
		val->intval = info->vbat;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		strategy_class_fg_ops_get_current(&info->ibat);
		val->intval = info->ibat;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		break;
	case POWER_SUPPLY_PROP_TYPE:
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		break;
	default:
		mca_log_err("Get prop %d is not supported in wireless\n", psp);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		mca_log_err("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int wireless_psy_set_prop(struct power_supply *psy,
				 enum power_supply_property psp,
				 const union power_supply_propval *val)
{
	int rc = 0;
	struct wireless_psy_info *info = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		info->online = val->intval;
		break;
	default:
		mca_log_err("Set prop %d is not supported in wireless psy\n",
			    psp);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		mca_log_err("Couldn't set prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int wireless_psy_prop_is_writeable(struct power_supply *psy,
					  enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		return 1;
	default:
		break;
	}

	return 0;
}

static struct power_supply_desc _wireless_psy_desc = {
	.name = "wireless",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = wireless_psy_props,
	.num_properties = ARRAY_SIZE(wireless_psy_props),
	.get_property = wireless_psy_get_prop,
	.set_property = wireless_psy_set_prop,
	.property_is_writeable = wireless_psy_prop_is_writeable,
};

void business_charger_update_wireless_psy(struct wireless_psy_info *info)
{
	power_supply_changed(info->wireless_psy);
	if (!info->batt_psy)
		info->batt_psy = power_supply_get_by_name("battery");

	if (info->batt_psy)
		power_supply_changed(info->batt_psy);
}

static int charger_wireless_psy_init(struct wireless_psy_info *info)
{
	struct power_supply_config wireless_cfg = {};
	int rc = 0;

	wireless_cfg.drv_data = info;
	wireless_cfg.of_node = info->dev->of_node;
	info->wireless_psy = devm_power_supply_register(
		info->dev, &_wireless_psy_desc, &wireless_cfg);
	if (IS_ERR(info->wireless_psy)) {
		mca_log_err("Couldn't register wireless power supply\n");
		return PTR_ERR(info->wireless_psy);
	}
	info->wireless_psy_desc = &_wireless_psy_desc;
	mca_log_err("wls psy success\n");

	return rc;
}

struct wireless_psy_info *business_charger_wireless_psy_init(struct device *dev)
{
	struct wireless_psy_info *info;
	int rc = 0;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;

	info->dev = dev;
	rc = charger_wireless_psy_init(info);
	if (rc < 0) {
		mca_log_err("Couldn't init wireless psy rc=%d\n", rc);
		return NULL;
	}

	info->batt_psy = power_supply_get_by_name("battery");
	if (!info->batt_psy)
		mca_log_err("get batt psy fail\n");

	return info;
}

void business_charger_wireless_psy_deinit(struct wireless_psy_info *info)
{
	if (!info || !info->wireless_psy)
		return;

	if (info->batt_psy) {
		power_supply_put(info->batt_psy);
		info->batt_psy = NULL;
	}
}
