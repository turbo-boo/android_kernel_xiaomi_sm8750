/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_charger_wireless_psy.h
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

#ifndef __CHARGER_WIRELESS_PSY_H__
#define __CHARGER_WIRELESS_PSY_H__

#include <linux/device.h>
#include <linux/power_supply.h>

struct wireless_psy_info {
	struct device *dev;
	struct power_supply_desc *wireless_psy_desc;
	struct power_supply *wireless_psy;
	struct power_supply *batt_psy;
	int present;
	int online;
	int ibat;
	int vbat;
};

struct wireless_psy_info *
business_charger_wireless_psy_init(struct device *dev);
void business_charger_wireless_psy_deinit(struct wireless_psy_info *info);
void business_charger_update_wireless_psy(struct wireless_psy_info *info);

#endif /* __CHARGER_WIRELESS_PSY_H__ */
