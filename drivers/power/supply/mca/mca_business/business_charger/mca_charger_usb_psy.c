// SPDX-License-Identifier: GPL-2.0
/*
 * mca_charger_usb_psy.c
 *
 * mc charger usb psy driver
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
#include <mca/protocol/protocol_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include "inc/mca_charger_usb_psy.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "charger_usb_psy"
#endif

static enum power_supply_property usb_psy_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_TYPE,
};

static int usb_psy_get_prop(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	int rc = 0;
	int bus_vol, bus_curr = 0;
	struct usb_psy_info *info = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_BUCK_CHARGE,
			STRATEGY_STATUS_TYPE_ONLINE, &info->present);
		val->intval = info->present;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_BUCK_CHARGE,
			STRATEGY_STATUS_TYPE_ONLINE, &info->online);
		val->intval = info->online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		(void)platform_class_buckchg_ops_get_usb_sns_volt(
			MAIN_BUCK_CHARGER, &bus_vol);
		val->intval = bus_vol;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		(void)platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER,
							      &bus_vol);
		val->intval = bus_vol;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		(void)platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER,
							      &bus_curr);
		val->intval = bus_curr;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		break;
	case POWER_SUPPLY_PROP_TYPE:
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		break;
	default:
		mca_log_err("Get prop %d is not supported in usb\n", psp);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		mca_log_err("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int usb_psy_set_prop(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *pval)
{
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		mca_strategy_func_set_config(
			STRATEGY_FUNC_TYPE_BUCK_CHARGE,
			STRATEGY_CONFIG_INPUT_CURRENT_LIMIT, pval->intval);
		break;
	default:
		mca_log_err("set prop %d is not supported in usb\n", psp);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		mca_log_err("Couldn't set prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int usb_psy_prop_is_writeable(struct power_supply *psy,
				     enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static struct power_supply_desc _usb_psy_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.properties = usb_psy_props,
	.num_properties = ARRAY_SIZE(usb_psy_props),
	.get_property = usb_psy_get_prop,
	.set_property = usb_psy_set_prop,
	.property_is_writeable = usb_psy_prop_is_writeable,
};

void business_charger_update_usb_psy(int real_type, struct usb_psy_info *info)
{
	switch (real_type) {
	case XM_CHARGER_TYPE_SDP:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_USB;
		break;
	case XM_CHARGER_TYPE_CDP:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case XM_CHARGER_TYPE_FLOAT:
	case XM_CHARGER_TYPE_DCP:
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
	case XM_CHARGER_TYPE_OCP:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case XM_CHARGER_TYPE_TYPEC:
	case XM_CHARGER_TYPE_PD:
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_USB_PD;
		break;
	case XM_CHARGER_TYPE_ACA:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_USB_ACA;
		break;
	default:
		info->usb_psy_desc->type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	}

	power_supply_changed(info->usb_psy);
	if (!info->batt_psy)
		info->batt_psy = power_supply_get_by_name("battery");

	if (info->batt_psy)
		power_supply_changed(info->batt_psy);
}

void business_usb_psy_event_process(struct usb_psy_info *info)
{
	if (!info)
		return;

	power_supply_changed(info->usb_psy);
}

static int charger_usb_psy_init(struct usb_psy_info *info)
{
	struct power_supply_config usb_cfg = {};
	int rc = 0;

	usb_cfg.drv_data = info;
	usb_cfg.of_node = info->dev->of_node;
	info->usb_psy =
		devm_power_supply_register(info->dev, &_usb_psy_desc, &usb_cfg);
	if (IS_ERR(info->usb_psy)) {
		mca_log_err("Couldn't register usb power supply\n");
		return PTR_ERR(info->usb_psy);
	}
	info->usb_psy_desc = &_usb_psy_desc;

	return rc;
}

struct usb_psy_info *business_charger_usb_psy_init(struct device *dev)
{
	struct usb_psy_info *info;
	int rc = 0;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;

	info->dev = dev;
	rc = charger_usb_psy_init(info);
	if (rc < 0) {
		pr_err("Couldn't init usb psy rc=%d\n", rc);
		return NULL;
	}

	info->batt_psy = power_supply_get_by_name("battery");
	if (!info->batt_psy)
		mca_log_err("get batt psy fail\n");

	mca_log_err("usb psy success\n");
	return info;
}

void business_charger_usb_psy_deinit(struct usb_psy_info *info)
{
	if (!info || !info->usb_psy)
		return;

	if (info->batt_psy) {
		power_supply_put(info->batt_psy);
		info->batt_psy = NULL;
	}
}
