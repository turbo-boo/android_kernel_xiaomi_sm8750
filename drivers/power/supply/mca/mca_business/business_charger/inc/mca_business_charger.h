/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_business_charger.h
 *
 * mca charge business driver
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
#ifndef __CHARGER_CORE_H__
#define __CHARGER_CORE_H__

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <mca/strategy/strategy_class.h>
#include <mca/common/mca_voter.h>
#include "mca_charger_usb_psy.h"
#include "mca_charger_wireless_psy.h"

#define BATT_WARM_THRESHOLD 480
#define BATT_COOL_THRESHOLD 15
#define BATT_COLD_THRESHOLD 0

enum business_charger_attr_list {
	BUSINESS_CHARGER_DEBUG_PROP_DAM_TEST = 0,
};

struct chg_dt_props {
	int test;
};

struct charger_event_lis_node {
	struct list_head lnode;
	unsigned int event;
	void *data;
};

union charger_abnormal_info {
	u32 asuint32;
	struct {
		u32 batt_auth_failed : 1; /*bit [0:0]*/
		u32 batt_missing : 1; /*bit [2:2]*/
		u32 batt_cold : 1; /*bit [3:3]*/
		u32 batt_overhot : 1; /*bit [4:4]*/
		u32 reserved : 26; /*bit [31:5]*/
	};
};

struct business_charger {
	bool stopping;
	struct task_struct *event_thread;
	struct device *dev;
	struct chg_dt_props dt;
	struct usb_psy_info *usb_psy_info;
	struct wireless_psy_info *wls_psy_info;
	struct notifier_block connect_nb;
	struct notifier_block type_change_nb;
	struct notifier_block hw_info_nb;
	struct notifier_block chg_sts_nb;
	struct notifier_block cp_info_nb;
	struct notifier_block debug_nb;
	struct list_head header;
	spinlock_t list_lock;
	wait_queue_head_t wait_que;
	struct wakeup_source *online_wake_lock;
	struct delayed_work delay_report_status_work;
	struct delayed_work delay_enable_rx_work;
	struct delayed_work reset_rx_work;
	struct delayed_work report_quick_charge_type_work;
	struct delayed_work wireless_rerun_usb_sns_work;
	struct delayed_work pmic_init_done_notify_work;
	union charger_abnormal_info abnormal_info;
	int wired_qucik_charge_type;
	int wired_power_max;
	int bc12_active;
	int pd_active;
	int bc12_type;
	int pd_type;
	int real_type;
	int thread_active;
	int wls_active;
	int wls_adapter_type;
	int otg_present;
	int dam_test_flag;
	int is_eu_model;
	int plate_shock;
	int start_quick_revchg;
	int revchg_bcl;
	int handle_state;
	int stop_handle_charge;
	int wls_support;
};

#endif /* __CHARGER_CORE_H__ */
