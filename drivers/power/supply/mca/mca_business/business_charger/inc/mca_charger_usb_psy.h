/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_charger_usb_psy.h
 *
 * mca usb psy driver
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
#ifndef __CHARGER_USB_PSY_H__
#define __CHARGER_USB_PSY_H__

#include <linux/device.h>
#include <linux/power_supply.h>

struct usb_psy_info {
	struct device *dev;
	struct power_supply_desc *usb_psy_desc;
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
	int present;
	int online;
	int charge_enable;
};

struct usb_psy_info *business_charger_usb_psy_init(struct device *dev);
void business_charger_usb_psy_deinit(struct usb_psy_info *info);
void business_charger_update_usb_psy(int real_type, struct usb_psy_info *info);
void business_usb_psy_event_process(struct usb_psy_info *info);

#endif /* __CHARGER_USB_PSY_H__ */
