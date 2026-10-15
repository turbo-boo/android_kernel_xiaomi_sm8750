// SPDX-License-Identifier: GPL-2.0
/*
 * mca_business_charger.c
 *
 * mc charger business driver
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
#include <mca/common/mca_workqueue.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/soc/qcom/fsa4480-i2c.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_wireless_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/shared_memory/charger_partition_class.h>
#include "inc/mca_business_charger.h"
#include "inc/mca_charger_usb_psy.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_business_charger"
#endif

extern int connector_antiburn_is_triggered(void);
extern int lpd_is_charging_limit(void);

#define BUSINESS_CHARGER_THREAD_ACTIVE 1

#define QUICK_CHG_TYPE_FLASH_CHG_POWER 20
#define QUICK_CHG_TYPE_TURBO_CHG_POWER 30
#define QUICK_CHG_TYPE_SUPER_CHG_POWER 50

static int g_last_double85;
static int g_last_remove_temp_limit;
static int g_last_memory_test;
static int g_last_soc_limit_hi;
static int g_last_soc_limit_lo;

static void business_charger_report_wired_quick_charge_type(
	struct business_charger *charger, int icon_type);

static void update_usb_pd_type(struct business_charger *charger)
{
	static int last_type;
	int cur_type;

	protocol_class_get_adapter_type(ADAPTER_PROTOCOL_BC12,
					&charger->bc12_type);
	protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &charger->pd_type);
	mca_log_info("last_bc12_type = %d, bc12_type = %d, pd_type = %d\n",
		     last_type, charger->bc12_type, charger->pd_type);
	if (!charger->bc12_active) {
		cur_type = XM_CHARGER_TYPE_UNKNOW;
	} else {
		if (charger->pd_type)
			cur_type = charger->pd_type;
		else
			cur_type = charger->bc12_type;
	}

	if (last_type != cur_type) {
		mca_log_err("real_type changed: %d => %d\n", last_type,
			    cur_type);
		charger->real_type = cur_type;
		business_charger_update_usb_psy(charger->real_type,
						charger->usb_psy_info);
		if (charger->real_type == XM_CHARGER_TYPE_FLOAT)
			mca_charge_mievent_report(
				CHARGE_DFX_NOT_STANDARD_ADAPTER, NULL, 0);
		last_type = cur_type;
	}
}

static int business_charger_parse_dt(struct business_charger *charger)
{
	struct device_node *node = charger->dev->of_node;
	int ret;

	(void)mca_parse_dts_u32(node, "support-wls", &charger->wls_support, 0);
	mca_log_info("support-wls %d\n", charger->wls_support);

	ret = of_property_read_u32(node, "business,charger-core-test",
				   &charger->dt.test);
	if (ret < 0) {
		mca_log_err("not find property %s\n",
			    "business,charger-core-test");
		return ret;
	}
	mca_log_info("%s: %d\n", "business,charger-core-test",
		     charger->dt.test);

	return 0;
}

static void business_charger_add_event_node(unsigned long event, void *data,
					    int size,
					    struct business_charger *charger)
{
	struct charger_event_lis_node *event_node = NULL;

	if (READ_ONCE(charger->stopping))
		return;
	event_node = kmalloc(sizeof(*event_node), GFP_ATOMIC);
	if (!event_node)
		return;

	event_node->event = event;
	event_node->data = NULL;
	if (data) {
		event_node->data = kmemdup(data, size, GFP_ATOMIC);
		if (!event_node->data) {
			kfree(event_node);
			return;
		}
	}
	spin_lock(&charger->list_lock);
	list_add_tail(&event_node->lnode, &charger->header);
	spin_unlock(&charger->list_lock);
	charger->thread_active = BUSINESS_CHARGER_THREAD_ACTIVE;
	wake_up_interruptible(&charger->wait_que);
}

static int business_charger_notifier_connect_cb(struct notifier_block *nb,
						unsigned long event, void *data)
{
	struct business_charger *charger =
		container_of(nb, struct business_charger, connect_nb);
	int size = 0;

	switch (event) {
	case MCA_EVENT_WIRELESS_REVCHG:
		size = sizeof(int);
		break;
	case MCA_EVENT_USB_SUSPEND:
		size = sizeof(bool);
		break;
	default:
		break;
	}
	mca_log_info("event %lu\n", event);
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static int business_charger_notifier_type_cb(struct notifier_block *nb,
					     unsigned long event, void *data)
{
	struct business_charger *charger =
		container_of(nb, struct business_charger, type_change_nb);
	int size = 0;

	switch (event) {
	case MCA_EVENT_CHARGE_TYPE_CHANGE:
	case MCA_EVENT_CHARGE_VERIFY_PROCESS_END:
		size = sizeof(int);
		break;
	case MCA_EVENT_SINK_PWR_SUSPEND_CHANGE:
		size = sizeof(bool);
		break;
	default:
		break;
	}

	mca_log_info("event %lu\n", event);
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static int business_charger_notifier_hw_cb(struct notifier_block *nb,
					   unsigned long event, void *data)
{
	struct business_charger *charger =
		container_of(nb, struct business_charger, hw_info_nb);
	int size = 0;

	switch (event) {
	case MCA_EVENT_BATT_BTB_CHANGE:
	case MCA_EVENT_LPD_STATUS_CHANGE:
	case MCA_EVENT_CC_SHORT_VBUS:
	case MCA_EVENT_VBAT_OVP_CHANGE:
	case MCA_EVENT_IBAT_OCP_CHANGE:
	case MCA_EVENT_CP_REVERT_CHANGE:
		size = sizeof(int);
		break;
	default:
		break;
	}
	mca_log_info("event %lu\n", event);
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static int business_charger_notifier_chg_sts_cb(struct notifier_block *nb,
						unsigned long event, void *data)
{
	struct business_charger *charger =
		container_of(nb, struct business_charger, chg_sts_nb);
	int size = 0;

	mca_log_info("event %lu\n", event);
	switch (event) {
	case MCA_EVENT_SOC_LIMIT:
	case MCA_EVENT_BATTERY_DTPT:
	case MCA_EVENT_CSD_SEND_PULSE:
		size = sizeof(int);
		break;
	default:
		break;
	}
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static int business_charger_notifier_cp_info_cb(struct notifier_block *nb,
						unsigned long event, void *data)
{
	struct business_charger *charger =
		container_of(nb, struct business_charger, cp_info_nb);
	int size = 0;

	mca_log_info("event %lu\n", event);
	switch (event) {
	case MCA_EVENT_CP_TSHUT_FLAG:
		size = sizeof(int) * 2;
		break;
	case MCA_EVENT_CP_CBOOT_FAIL:
		size = sizeof(int);
		break;
	default:
		break;
	}
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static int business_charger_notifier_debug_cb(struct notifier_block *nb,
					      unsigned long event, void *data)
{
	struct business_charger *charger =
		container_of(nb, struct business_charger, debug_nb);
	int size = 0;

	switch (event) {
	case MCA_EVENT_DEBUG_CTRL_DOUBLE85:
	case MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT:
	case MCA_EVENT_DEBUG_CTRL_MEMORY_TEST:
	case MCA_EVENT_DEBUG_CTRL_SOC_LIMIT:
		size = sizeof(int);
		break;
	default:
		break;
	}
	mca_log_info("debug event %lu\n", event);
	business_charger_add_event_node(event, data, size, charger);

	return NOTIFY_OK;
}

static void
business_charger_wireless_report_status_work(struct work_struct *work)
{
	struct business_charger *charger = container_of(
		work, struct business_charger, delay_report_status_work.work);
	if (READ_ONCE(charger->stopping))
		return;

	int len = 0, ret = 0;
	int usb_online = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	union power_supply_propval prop;

	mca_log_info("wireless delay report status work\n");

	prop.intval = charger->wls_active;
	ret = power_supply_set_property(charger->wls_psy_info->wireless_psy,
					POWER_SUPPLY_PROP_ONLINE, &prop);
	if (ret < 0)
		mca_log_err("failed to set wls_online");
	business_charger_update_wireless_psy(charger->wls_psy_info);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		       "POWER_SUPPLY_TX_ADAPTER=0");
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
				  MCA_EVENT_WIRELESS_DISCONNECT,
				  charger->wls_active);

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					   STRATEGY_STATUS_TYPE_ONLINE,
					   &usb_online);
	if (usb_online)
		return;

	__pm_relax(charger->online_wake_lock);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		       "POWER_SUPPLY_QUICK_CHARGE_TYPE=0");
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

static void
business_charger_wireless_delay_enable_rx_work(struct work_struct *work)
{
	int usb_online = 0;

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					   STRATEGY_STATUS_TYPE_ONLINE,
					   &usb_online);
	if (!usb_online)
		platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER,
							true);

	mca_log_info("delay_enable_rx when usb out\n");
}

static void business_charger_wireless_reset_rx_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	int usb_present = 0;

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					   STRATEGY_STATUS_TYPE_ONLINE,
					   &usb_present);
	mca_log_info("get usb_present %d\n", usb_present);
	if (!usb_present) {
		platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER,
							false);
		usleep_range(125000, 150000);
		platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER,
							true);
	}
}

static void
business_charger_process_cap_change(struct business_charger *charger,
				    unsigned int event)
{
	if (charger->real_type == XM_CHARGER_TYPE_PPS) {
		cancel_delayed_work_sync(
			&charger->report_quick_charge_type_work);
		schedule_delayed_work(&charger->report_quick_charge_type_work,
				      0);
	} else {
		cancel_delayed_work_sync(
			&charger->report_quick_charge_type_work);
		schedule_delayed_work(&charger->report_quick_charge_type_work,
				      msecs_to_jiffies(250));
	}

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event, 0);
}

static void
business_charger_process_snk_power_suspend(struct business_charger *charger,
					   unsigned int event, void *data)
{
	bool bsinkpowersuspend = *((bool *)data);

	mca_log_info("bsinkpowersuspend: %d\n", bsinkpowersuspend);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  bsinkpowersuspend);
}

static void business_charger_report_wired_quick_charge_type(
	struct business_charger *charger, int icon_type)
{
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data;
	int len;

	charger->wired_qucik_charge_type = icon_type;

	if (charger->abnormal_info.asuint32) {
		icon_type = ADP_ICON_TYPE_NORMAL;
		mca_log_info("force normal icon 0x%x",
			     charger->abnormal_info.asuint32);
	}

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		       "POWER_SUPPLY_QUICK_CHARGE_TYPE=%d", icon_type);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

static void
business_charger_report_quick_charge_type_work(struct work_struct *work)
{
	struct business_charger *charger =
		container_of(work, struct business_charger,
			     report_quick_charge_type_work.work);
	if (READ_ONCE(charger->stopping))
		return;

	int power_max;
	int icon_type;

	if (charger->real_type != XM_CHARGER_TYPE_PPS)
		return;

	protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS,
					     (unsigned int *)(&power_max));
	charger->wired_power_max = power_max;
	if (power_max >= QUICK_CHG_TYPE_SUPER_CHG_POWER)
		icon_type = ADP_ICON_TYPE_SUPER;
	else if (power_max >= QUICK_CHG_TYPE_TURBO_CHG_POWER)
		icon_type = ADP_ICON_TYPE_TURBO;
	else if (power_max >= QUICK_CHG_TYPE_FLASH_CHG_POWER)
		icon_type = ADP_ICON_TYPE_FLASH;
	else
		icon_type = ADP_ICON_TYPE_FAST;

	mca_log_info("power_max: %d, icon_type: %d\n", power_max, icon_type);
	business_charger_report_wired_quick_charge_type(charger, icon_type);
}

static void business_charger_update_wired_quick_charge_type(
	struct business_charger *charger)
{
	union power_supply_propval pval = {
		0,
	};
	int icon_type = 0;

	if (!charger->bc12_active)
		return;

	if (charger->usb_psy_info != NULL &&
	    charger->usb_psy_info->batt_psy != NULL)
		power_supply_get_property(charger->usb_psy_info->batt_psy,
					  POWER_SUPPLY_PROP_HEALTH, &pval);
	if (pval.intval == POWER_SUPPLY_HEALTH_COLD)
		charger->abnormal_info.batt_cold = 1;
	else if (pval.intval == POWER_SUPPLY_HEALTH_OVERHEAT)
		charger->abnormal_info.batt_overhot = 1;
	else {
		charger->abnormal_info.batt_cold = 0;
		charger->abnormal_info.batt_overhot = 0;
	}

	switch (charger->real_type) {
	case XM_CHARGER_TYPE_SDP:
	case XM_CHARGER_TYPE_CDP:
	case XM_CHARGER_TYPE_DCP:
	case XM_CHARGER_TYPE_FLOAT:
	case XM_CHARGER_TYPE_TYPEC:
	case XM_CHARGER_TYPE_ACA:
	case XM_CHARGER_TYPE_OCP:
		icon_type = ADP_ICON_TYPE_NORMAL;
		break;
	case XM_CHARGER_TYPE_PD:
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
		icon_type = ADP_ICON_TYPE_FAST;
		break;
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		icon_type = ADP_ICON_TYPE_FLASH;
		break;
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		icon_type = ADP_ICON_TYPE_FAST;
		protocol_class_get_adapter_max_power(
			ADAPTER_PROTOCOL_PPS,
			(unsigned int *)&charger->wired_power_max);
		break;
	default:
		break;
	}

	business_charger_report_wired_quick_charge_type(charger, icon_type);
}

static void
business_charger_process_type_change(struct business_charger *charger,
				     unsigned int event, void *data)
{
	(void)update_usb_pd_type(charger);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  charger->real_type);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  charger->real_type);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, event,
				  charger->real_type);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event,
				  charger->real_type);
	if (charger->real_type != XM_CHARGER_TYPE_PPS)
		business_charger_update_wired_quick_charge_type(charger);
}

static void business_charger_process_wireless_online_change(
	struct business_charger *charger, unsigned int event)
{
	static int wls_active_flag;
	int ret = 0;
	union power_supply_propval prop;

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
					   STRATEGY_STATUS_TYPE_ONLINE,
					   &charger->wls_active);
	mca_log_info("wireless online %d\n", charger->wls_active);
	if (wls_active_flag == charger->wls_active)
		return;

	wls_active_flag = charger->wls_active;
	if (!wls_active_flag) {
		charger->wls_adapter_type = 0;
		event = MCA_EVENT_WIRELESS_DISCONNECT;
		mca_log_info("relax wake lock\n");
		mca_charge_mievent_set_state(MIEVENT_STATE_PLUG, 0);
	} else {
		if (!charger->dam_test_flag) {
			mca_log_info("stay wake lock\n");
			event = MCA_EVENT_WIRELESS_CONNECT;
			__pm_stay_awake(charger->online_wake_lock);
		}
	}

	if (wls_active_flag) {
		prop.intval = charger->wls_active;
		ret = power_supply_set_property(
			charger->wls_psy_info->wireless_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
		if (ret < 0)
			mca_log_err("failed to set wls_online");
		cancel_delayed_work_sync(&charger->delay_report_status_work);
		business_charger_update_wireless_psy(charger->wls_psy_info);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
					  wls_active_flag);
	} else
		schedule_delayed_work(&charger->delay_report_status_work,
				      msecs_to_jiffies(500));

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event,
				  wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event,
				  wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event,
				  wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_JEITA, event,
				  wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, event,
				  wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_SMARTCHG, event,
				  wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event,
				  wls_active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BMD, event,
				  wls_active_flag);
}

static void
business_charger_process_usb_sns_func(struct business_charger *charger,
				      unsigned int event, void *data)
{
	int usb_present = *((int *)data);
	bool fw_update = false;

	mca_wireless_rev_get_fw_update(&fw_update);
	mca_log_err("usb_resent: %d, fw_update: %d\n", usb_present, fw_update);

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event,
				  usb_present);

	if (!usb_present) {
		if (!fw_update)
			schedule_delayed_work(&charger->delay_enable_rx_work,
					      msecs_to_jiffies(375));
	} else {
		platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER,
							false);
	}
	mca_wireless_rev_set_usb_plugin(usb_present != 0);
}

static void
business_charger_wireless_rerun_usb_sns_work(struct work_struct *work)
{
	struct business_charger *charger =
		container_of(work, struct business_charger,
			     wireless_rerun_usb_sns_work.work);
	if (READ_ONCE(charger->stopping))
		return;

	int usb_present = 0;

	mca_log_info("wireless rerun cp vusb work\n");
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					   STRATEGY_STATUS_TYPE_ONLINE,
					   &usb_present);
	business_charger_process_usb_sns_func(charger, (usb_present != 0),
					      &usb_present);
}

static void
business_charger_process_online_change(struct business_charger *charger,
				       unsigned int event)
{
	static int active_flag;
	struct mca_event_notify_data n_data = { 0 };
	union power_supply_propval pval = {
		0,
	};

	power_supply_get_property(charger->wls_psy_info->wireless_psy,
				  POWER_SUPPLY_PROP_ONLINE, &pval);

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					   STRATEGY_STATUS_TYPE_ONLINE,
					   &charger->bc12_active);
	mca_log_info("online %d\n", charger->bc12_active);

	active_flag = charger->bc12_active;
	if (!active_flag) {
		charger->pd_type = 0;
		charger->real_type = 0;
		charger->dam_test_flag = 0;
		charger->wired_qucik_charge_type = 0;
		charger->wired_power_max = 0;
		event = MCA_EVENT_USB_DISCONNECT;
		__pm_relax(charger->online_wake_lock);
		mca_charge_mievent_set_state(MIEVENT_STATE_PLUG, 0);
		mca_log_info("relax wake lock\n");
	} else if (!charger->dam_test_flag) {
		mca_log_info("stay wake lock\n");
		event = MCA_EVENT_USB_CONNECT;
		__pm_stay_awake(charger->online_wake_lock);
	}
	(void)update_usb_pd_type(charger);
	business_charger_process_usb_sns_func(charger, event, &active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_JEITA, event, active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, event, active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_SMARTCHG, event,
				  active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, event,
				  active_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BMD, event, active_flag);
	if (active_flag) {
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					  MCA_EVENT_CHARGE_TYPE_CHANGE,
					  charger->real_type);
	} else if (!pval.intval) {
		n_data.event = "POWER_SUPPLY_QUICK_CHARGE_TYPE=0";
		n_data.event_len =
			32; /* length of POWER_SUPPLY_QUICK_CHARGE_TYPE=0 */
		mca_event_report_uevent(&n_data);
	}
}

static void
business_charger_process_usb_suspend_change(struct business_charger *charger,
					    unsigned int event, void *data)
{
	bool usb_suspend = *((bool *)data);

	if (usb_suspend) {
		__pm_relax(charger->online_wake_lock);
		mca_log_info("relax wake lock\n");
	} else {
		if (charger->bc12_active) {
			__pm_stay_awake(charger->online_wake_lock);
			mca_log_info("stay wake lock\n");
		}
	}
}

static void
business_charger_process_is_eu_model(struct business_charger *charger,
				     int value)
{
	mca_log_info("is_eu_model: %d\n", value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, MCA_EVENT_IS_EU_MODEL,
				  value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				  MCA_EVENT_IS_EU_MODEL, value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
				  MCA_EVENT_IS_EU_MODEL, value);
}

static void
business_charger_process_plate_shock(struct business_charger *charger,
				     int value)
{
	mca_log_info("plate_shock: %d\n", value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				  MCA_EVENT_PLATE_SHOCK, value == 1 ? 1 : 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
				  MCA_EVENT_PLATE_SHOCK, value == 1 ? 1 : 0);
}

static void
business_charger_process_start_quick_revchg(struct business_charger *charger,
					    int value)
{
	mca_log_info("start_quick_revchg: %d\n", value);
	if (!value) {
		mca_log_info("user cancel start_quick_revchg, do nothing\n");
		return;
	}
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
				  MCA_EVENT_START_QUICK_REVCHG, value);
}

static void
business_charger_process_set_revchg_bcl(struct business_charger *charger,
					int value)
{
	mca_log_info("set_revchg_bcl: %d\n", value);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
				  MCA_EVENT_REVCHG_BCL, value);
}

static void
business_charger_process_handle_logic(struct business_charger *charger)
{
	static int last_handle_state;
	static int last_stop_handle_charge;
	int stop = charger->stop_handle_charge;
	int hs = charger->handle_state;
	int allow_charge = -1;

	if (last_stop_handle_charge != 0 && stop == 0 &&
	    hs != 0) {
		/* stop just cleared while a handle-state is set: hold off */
		allow_charge = -1;
	} else if (last_stop_handle_charge != 0 && stop == 0 &&
		   hs == 0) {
		allow_charge = 1;
	} else if (stop != 0 &&
		   last_stop_handle_charge != stop) {
		allow_charge = 1;
	}

	if (hs == 0 && last_handle_state != 0)
		allow_charge = 1;
	if (stop == 0 && hs != 0 && last_handle_state != hs)
		allow_charge = 0;

	mca_log_info(
		"last_allow_charge = %d, last_handle_state = %d, last_stop_handle_charge = %d, allow_charge = %d, handle_state = %d, stop_handle_charge = %d\n",
		charger->usb_psy_info->charge_enable,
		last_handle_state,
		last_stop_handle_charge, allow_charge, hs, stop);

	last_handle_state = charger->handle_state;
	last_stop_handle_charge = charger->stop_handle_charge;

	if (allow_charge != -1 &&
	    allow_charge != charger->usb_psy_info->charge_enable) {
		charger->usb_psy_info->charge_enable = allow_charge;
		business_usb_psy_event_process(charger->usb_psy_info);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					  MCA_EVENT_HANDLE_ALLOW_CHARGE,
					  allow_charge);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					  MCA_EVENT_HANDLE_ALLOW_CHARGE,
					  allow_charge);
		if (allow_charge == 0) {
			if (charger->real_type != 0) {
				struct mca_event_notify_data n_data = { 0 };

				n_data.event =
					"POWER_SUPPLY_QUICK_CHARGE_TYPE=0";
				n_data.event_len = 32;
				mca_event_report_uevent(&n_data);
			}
		} else if (charger->real_type != XM_CHARGER_TYPE_PPS) {
			business_charger_update_wired_quick_charge_type(
				charger);
		}
	}
}

static void
business_charger_process_lpd_status_change(struct business_charger *charger,
					   unsigned int event, void *data)
{
	int force_5v_charging = 0;
	int is_liquited = *((int *)data);
	int is_charginglimit = lpd_is_charging_limit();

	force_5v_charging = is_liquited & is_charginglimit;

	mca_log_info("%s: occur liquited need drop charging power: %d\n",
		     __func__, force_5v_charging);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  force_5v_charging);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  force_5v_charging);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event,
				  is_liquited);
	mca_charge_mievent_report(CHARGE_DFX_LPD_DETECTED, &is_liquited, 1);
}

static void
business_charger_process_verify_process_change(struct business_charger *charger,
					       unsigned int event, void *data)
{
	int verify_process_end = *((int *)data);

	mca_log_info("pd verify process done: %d\n", !verify_process_end);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  !verify_process_end);
}

static void
business_charger_process_pmic_init_done(struct business_charger *charger)
{
	mca_log_err("pmic init done\n");
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
				  MCA_EVENT_PMIC_INIT_DONE, 1);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
				  MCA_EVENT_PMIC_INIT_DONE, 1);
	if (charger->wls_support)
		schedule_delayed_work(&charger->pmic_init_done_notify_work, 0);
}

static void
business_charger_pmic_init_done_notify_work(struct work_struct *work)
{
	struct business_charger *charger = container_of(
		work, struct business_charger, pmic_init_done_notify_work.work);
	if (READ_ONCE(charger->stopping))
		return;

	int wls_init = 0;
	int buck_init = 0;

	mca_log_info("pmic init done notify work\n");

	if (!charger->wls_support)
		wls_init = 1;
	else
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_CHARGING, &wls_init);
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					   STRATEGY_STATUS_TYPE_CHARGING,
					   &buck_init);

	if (!wls_init || !buck_init) {
		mca_log_err("wls_basic or buckchg not init done, rerun work\n");
		schedule_delayed_work(&charger->pmic_init_done_notify_work,
				      msecs_to_jiffies(250));
		return;
	}

	if (platform_class_buckchg_ops_is_init_ok(MAIN_BUCK_CHARGER) < 1) {
		mca_log_err("pmic not init done, return & wait event\n");
		return;
	}
	mca_log_err("pmic init done, process event\n");
	business_charger_process_pmic_init_done(charger);
}

static void
business_charger_process_cp_cboot_short(struct business_charger *charger,
					unsigned int event, void *data)
{
	int cp_cboot_short = *((int *)data);

	mca_log_info("occur cp cboot short: %d\n", cp_cboot_short);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  cp_cboot_short);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  cp_cboot_short);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event,
				  cp_cboot_short);
}

static void
business_charger_process_quick_revchg(struct business_charger *charger,
				      unsigned int event, void *data)
{
	int auth_pos = *((int *)data);

	mca_log_info("handle quick revchg changed: %#x\n", auth_pos);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  auth_pos);
}

static void
business_charger_process_rp_short_vbus_change(struct business_charger *charger,
					      unsigned int event, void *data)
{
	int cc_short_vbus = *((int *)data);

	mca_log_info("occur cc short vbus: %d\n", cc_short_vbus);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  cc_short_vbus);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  cc_short_vbus);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event,
				  cc_short_vbus);
	if (cc_short_vbus)
		mca_charge_mievent_report(CHARGE_DFX_RP_SHORT_VBUS_DETECTED,
					  NULL, 0);
}

static void business_charger_process_vbat_ovp_status_change(
	struct business_charger *charger, unsigned int event, void *data)
{
	int vbat_ovp_status = *((int *)data);

	mca_log_info("occur vbat_ovp_status: %d\n", vbat_ovp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  vbat_ovp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  vbat_ovp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event,
				  vbat_ovp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event,
				  vbat_ovp_status);
}

static void business_charger_process_ibat_ocp_status_change(
	struct business_charger *charger, unsigned int event, void *data)
{
	int ibat_ocp_status = *((int *)data);

	mca_log_info("occur ibat_ocp_status: %d\n", ibat_ocp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  ibat_ocp_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event,
				  ibat_ocp_status);
}

static void
business_charger_process_csd_pulse_change(struct business_charger *charger,
					  unsigned int event, void *data)
{
	int csd_pulse = *((int *)data);

	if (charger->real_type == XM_CHARGER_TYPE_PPS) {
		mca_log_info("receive csd pulse request: %d\n", csd_pulse);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					  event, csd_pulse);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
					  csd_pulse);
	} else
		mca_log_info(
			"not standard pd verify, ignore csd pulse event\n");
}

static void
business_charger_process_soc_limit_change(struct business_charger *charger,
					  unsigned int event, void *data)
{
	int smartchg_pmic = *((int *)data);

	mca_log_info("smartchg_pmic: %d\n", smartchg_pmic);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  smartchg_pmic);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  smartchg_pmic);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event,
				  smartchg_pmic);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event,
				  smartchg_pmic);
}

static void
business_charger_process_dtpt_change(struct business_charger *charger,
				     unsigned int event, void *data)
{
	int dtpt_status = *((int *)data);

	mca_log_info("dtpt_status: %d\n", dtpt_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_SMARTCHG, event,
				  dtpt_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_JEITA, event, dtpt_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  dtpt_status);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event,
				  dtpt_status);
}

static void business_charger_process_fg_ota(struct business_charger *charger,
					    unsigned int event, void *data)
{
	int fg_ota_process = *((int *)data);

	mca_log_info("fg_ota_process: %d\n", fg_ota_process);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  fg_ota_process);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event,
				  fg_ota_process);
}

static void
business_charger_process_batt_health_change(struct business_charger *charger,
					    unsigned int event)
{
	business_charger_update_wired_quick_charge_type(charger);
	if (charger->real_type == XM_CHARGER_TYPE_PPS)
		schedule_delayed_work(&charger->report_quick_charge_type_work,
				      0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event, 0);
}

static void
business_charger_process_batt_btb_change(struct business_charger *charger,
					 unsigned int event, void *data)
{
	bool batt_missing = *((bool *)data);

	mca_log_info("batt_missing: %d\n", batt_missing);
	charger->abnormal_info.batt_missing = (int)batt_missing;
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  batt_missing);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  batt_missing);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event,
				  batt_missing);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event,
				  batt_missing);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event,
				  batt_missing);
}

static void
business_charger_process_batt_auth_pass(struct business_charger *charger,
					unsigned int event)
{
	mca_log_err("reviced batt_auth pass\n");
	charger->abnormal_info.batt_auth_failed = 0;
	business_charger_update_wired_quick_charge_type(charger);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, true);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, true);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event,
				  true);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event,
				  true);
}

static void
business_charger_process_chg_sts_change(struct business_charger *charger,
					unsigned int event)
{
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, event, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event, 0);
}

static void
business_charger_process_antiburn_change(struct business_charger *charger,
					 unsigned int event)
{
	int antiburn = connector_antiburn_is_triggered();

	mca_log_info("antiburn: %d\n", antiburn);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, event,
				  antiburn);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, event,
				  antiburn);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS, event,
				  antiburn);
}

static void business_charger_mievent_report_cp_vbat_ovp(void)
{
	int event_data[2] = { 0 };
	int vcell, vpack;

	(void)strategy_class_fg_ops_get_voltage(&vcell);
	(void)platform_class_cp_get_battery_voltage(CP_ROLE_MASTER, &vpack);
	event_data[0] = vcell;
	event_data[1] = vpack;
	mca_charge_mievent_report(CHARGE_DFX_CP_VBAT_OVP, event_data, 2);
}

static void
business_charger_process_debug_double85(struct business_charger *charger,
					int val)
{
	if (val == g_last_double85)
		return;
	g_last_double85 = val;
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL,
				  MCA_EVENT_DEBUG_CTRL_DOUBLE85, val);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG,
				  MCA_EVENT_DEBUG_CTRL_DOUBLE85, val);
	platform_class_buckchg_ops_set_too_hot_limit(MAIN_BUCK_CHARGER,
						     val == 0);
}

static void business_charger_process_debug_remove_temp_limit(
	struct business_charger *charger, int val)
{
	if (val == g_last_remove_temp_limit)
		return;
	g_last_remove_temp_limit = val;
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL,
				  MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT, val);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG,
				  MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT, val);
	platform_class_buckchg_ops_set_too_hot_limit(MAIN_BUCK_CHARGER,
						     val == 0);
}

static void
business_charger_process_debug_memory_test(struct business_charger *charger,
					   int val)
{
	if (val == g_last_memory_test)
		return;
	g_last_memory_test = val;
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL,
				  MCA_EVENT_DEBUG_CTRL_MEMORY_TEST, val);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG,
				  MCA_EVENT_DEBUG_CTRL_MEMORY_TEST, val);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
				  MCA_EVENT_DEBUG_CTRL_MEMORY_TEST, val);
	platform_class_buckchg_ops_set_too_hot_limit(MAIN_BUCK_CHARGER,
						     val == 0);
}

static void
business_charger_process_debug_soc_limit(struct business_charger *charger,
					 int packed)
{
	int hi = (packed >> 8) & 0xff;
	int lo = packed & 0xff;

	if (hi >= 0x65 || lo >= 0x65) {
		mca_log_err("soc_limit value is invalid: %d %d\n", hi, lo);
		return;
	}
	if (hi == g_last_soc_limit_hi && lo == g_last_soc_limit_lo)
		return;
	g_last_soc_limit_hi = hi;
	g_last_soc_limit_lo = lo;
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
				  MCA_EVENT_DEBUG_CTRL_SOC_LIMIT, packed);
}

static void
business_charger_process_sbu_lpd_status_change(struct business_charger *charger,
					       int value)
{
	struct of_phandle_args switch_args = { 0 };
	int sbu_event;
	int rc;

	rc = of_parse_phandle_with_args(charger->dev->of_node,
					"fsa4480-i2c-handle", NULL, 0,
					&switch_args);
	if (rc != 0 || switch_args.np == NULL) {
		mca_log_err("no switch node found\n");
		return;
	}

	mca_log_info("handle sbu lpd changed: %#x\n", value);
	if (value == 1)
		sbu_event = FSA_USBC_SBU_LPD_SENSE;
	else if (value == 2)
		sbu_event = FSA_USBC_SBU_LPD_ISOLATE;
	else
		sbu_event = FSA_USBC_DISPLAYPORT_DISCONNECTED;

	mca_log_err("third sbu event is %d\n", sbu_event);
	fsa4480_switch_event(switch_args.np, sbu_event);
	of_node_put(switch_args.np);
}

static void business_charger_process_event(struct business_charger *charger)
{
	struct charger_event_lis_node *event_node;
	int data[2] = { 0 };

	while (!READ_ONCE(charger->stopping)) {
		spin_lock(&charger->list_lock);
		if (list_empty(&charger->header)) {
			spin_unlock(&charger->list_lock);
			break;
		}
		event_node = list_first_entry(&charger->header, struct charger_event_lis_node, lnode);
		list_del(&event_node->lnode);
		spin_unlock(&charger->list_lock);

			mca_log_info("event_node->event: %d\n",
				     event_node->event);
			switch (event_node->event) {
			case MCA_EVENT_USB_DISCONNECT:
			case MCA_EVENT_USB_CONNECT:
				business_charger_process_online_change(
					charger, event_node->event);
				break;
			case MCA_EVENT_WIRELESS_CONNECT:
			case MCA_EVENT_WIRELESS_DISCONNECT:
				business_charger_process_wireless_online_change(
					charger, event_node->event);
				break;
			case MCA_EVENT_WIRELESS_REVCHG:
				mca_strategy_func_process(
					STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					event_node->event,
					*((int *)event_node->data));
				break;
			case MCA_EVENT_CHARGE_TYPE_CHANGE:
				business_charger_process_type_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_CHARGE_CAP_CHANGE:
				business_charger_process_cap_change(
					charger, event_node->event);
				break;
			case MCA_EVENT_CHARGE_VERIFY_PROCESS_END:
				mca_log_info(
					"receive verify_process_end notify: %d\n",
					*((int *)event_node->data));
				business_charger_process_verify_process_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_BATTERY_HEALTH_CHANGE:
				business_charger_process_batt_health_change(
					charger, event_node->event);
				break;
			case MCA_EVENT_CP_VUSB_OVP:
				mca_charge_mievent_report(CHARGE_DFX_CP_VAC_OVP,
							  NULL, 0);
				break;
			case MCA_EVENT_CP_VBAT_OVP:
				business_charger_mievent_report_cp_vbat_ovp();
				break;
			case MCA_EVENT_CP_VBUS_OVP:
				mca_charge_mievent_report(
					CHARGE_DFX_CP_VBUS_OVP, NULL, 0);
				break;
			case MCA_EVENT_CP_IBAT_OCP:
				mca_charge_mievent_report(
					CHARGE_DFX_CP_IBAT_OCP, NULL, 0);
				break;
			case MCA_EVENT_CP_IBUS_OCP:
				mca_charge_mievent_report(
					CHARGE_DFX_CP_IBUS_OCP, NULL, 0);
				break;
			case MCA_EVENT_CP_CBOOT_FAIL:
				business_charger_process_cp_cboot_short(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_CP_IIC_ERROR:
				mca_strategy_func_process(
					STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					event_node->event, 0);
				mca_charge_mievent_report(CHARGE_DFX_CP_ABSENT,
							  data, 2);
				break;
			case MCA_EVENT_CP_TSHUT_FLAG:
				if (charger->wls_support == 1) {
					platform_class_cp_get_tdie(
						CP_ROLE_MASTER,
						&((int *)event_node->data)[0]);
					platform_class_cp_get_tdie(
						CP_ROLE_SLAVE,
						&((int *)event_node->data)[1]);
				}
				mca_charge_mievent_report(
					CHARGE_DFX_CP_TDIE_HOT,
					event_node->data, 2);
				break;
			case MCA_EVENT_CONN_ANTIBURN_CHANGE:
				business_charger_process_antiburn_change(
					charger, event_node->event);
				break;
			case MCA_EVENT_BATT_BTB_CHANGE:
				business_charger_process_batt_btb_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_BATT_AUTH_PASS:
				business_charger_process_batt_auth_pass(
					charger, event_node->event);
				break;
			case MCA_EVENT_LPD_STATUS_CHANGE:
				business_charger_process_lpd_status_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_PMIC_INIT_DONE:
				business_charger_process_pmic_init_done(
					charger);
				break;
			case MCA_EVENT_CC_SHORT_VBUS:
				business_charger_process_rp_short_vbus_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_VBAT_OVP_CHANGE:
				business_charger_process_vbat_ovp_status_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_IBAT_OCP_CHANGE:
				business_charger_process_ibat_ocp_status_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_CP_REVERT_CHANGE:
				business_charger_process_quick_revchg(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_USB_STS_CHANGE:
				business_usb_psy_event_process(
					charger->usb_psy_info);
				break;
			case MCA_EVENT_CHARGE_ABNORMAL:
			case MCA_EVENT_CHARGE_RESTORE:
				business_charger_process_chg_sts_change(
					charger, event_node->event);
				break;
			case MCA_EVENT_SOC_LIMIT:
				business_charger_process_soc_limit_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_BATTERY_DTPT:
				business_charger_process_dtpt_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_CSD_SEND_PULSE:
				business_charger_process_csd_pulse_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_SINK_PWR_SUSPEND_CHANGE:
				business_charger_process_snk_power_suspend(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_USB_SUSPEND:
				business_charger_process_usb_suspend_change(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_FG_OTA_PROCESS:
				business_charger_process_fg_ota(
					charger, event_node->event,
					event_node->data);
				break;
			case MCA_EVENT_DEBUG_CTRL_DOUBLE85:
				business_charger_process_debug_double85(
					charger, *((int *)event_node->data));
				break;
			case MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT:
				business_charger_process_debug_remove_temp_limit(
					charger, *((int *)event_node->data));
				break;
			case MCA_EVENT_DEBUG_CTRL_MEMORY_TEST:
				business_charger_process_debug_memory_test(
					charger, *((int *)event_node->data));
				break;
			case MCA_EVENT_DEBUG_CTRL_SOC_LIMIT:
				business_charger_process_debug_soc_limit(
					charger, *((int *)event_node->data));
				break;
			case MCA_EVENT_SBU_LPD_CHANGE:
				business_charger_process_sbu_lpd_status_change(
					charger, *((int *)event_node->data));
				break;
			}
		kfree(event_node->data);
		kfree(event_node);
	}
}

/* Standard usb_type definitions similar to power_supply_sysfs.c */
static const char *const power_supply_usb_type_text[] = {
	"Unknown", "SDP",	"CDP",	     "DCP", "USB_FLOAT", "HVDCP",
	"HVDCP_3", "HVDCP_3_B", "HVDCP_3P5", "C",   "PD",	 "PD_PPS",
	"PD_PPS",  "Unknown",	"ACA",	     "DCP",
};

static const char *get_usb_type_name(u32 usb_type)
{
	if (usb_type < ARRAY_SIZE(power_supply_usb_type_text))
		return power_supply_usb_type_text[usb_type];

	return "Unknown";
}

enum bussiness_charger_attr_list {
	BUSSINESS_CHARGER_PROP_REAL_TYPE,
	BUSSINESS_CHARGER_PROP_POWER_MAX,
	BUSSINESS_CHARGER_PROP_QUICK_CHARGE_TYPE,
	BUSSINESS_CHARGER_PROP_IS_EU_MODEL,
	BUSSINESS_CHARGER_PROP_PLATE_SHOCK,
	BUSSINESS_CHARGER_PROP_START_QUICK_REVCHG,
	BUSSINESS_CHARGER_PROP_REVCHG_BCL,
	BUSSINESS_CHARGER_PROP_HANDLE_STATE,
	BUSSINESS_CHARGER_PROP_STOP_HANDLE_CHARGE,
	BUSSINESS_CHARGER_PROP_DEBUG_CTRL,
};

static ssize_t bussiness_charger_sysfs_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf);

static ssize_t bussiness_charger_sysfs_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count);

struct mca_sysfs_attr_info bussiness_charger_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(bussiness_charger_sysfs, 0444,
			  BUSSINESS_CHARGER_PROP_REAL_TYPE, real_type),
	mca_sysfs_attr_ro(bussiness_charger_sysfs, 0444,
			  BUSSINESS_CHARGER_PROP_POWER_MAX, power_max),
	mca_sysfs_attr_ro(bussiness_charger_sysfs, 0444,
			  BUSSINESS_CHARGER_PROP_QUICK_CHARGE_TYPE,
			  quick_charge_type),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664,
			  BUSSINESS_CHARGER_PROP_IS_EU_MODEL, is_eu_model),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664,
			  BUSSINESS_CHARGER_PROP_PLATE_SHOCK, plate_shock),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664,
			  BUSSINESS_CHARGER_PROP_START_QUICK_REVCHG,
			  start_quick_revchg),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664,
			  BUSSINESS_CHARGER_PROP_REVCHG_BCL, revchg_bcl),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664,
			  BUSSINESS_CHARGER_PROP_HANDLE_STATE, handle_state),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664,
			  BUSSINESS_CHARGER_PROP_STOP_HANDLE_CHARGE,
			  stop_handle_charge),
	mca_sysfs_attr_rw(bussiness_charger_sysfs, 0664,
			  BUSSINESS_CHARGER_PROP_DEBUG_CTRL, debug_ctrl),
};

#define BUSSINESS_CHARGER_SYSFS_ATTRS_SIZE \
	ARRAY_SIZE(bussiness_charger_sysfs_field_tbl)

static struct attribute
	*bussiness_charger_sysfs_attrs[BUSSINESS_CHARGER_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group bussiness_charger_sysfs_attr_group = {
	.attrs = bussiness_charger_sysfs_attrs,
};

static int business_charger_get_power_max(struct business_charger *charger,
					  int *pdata)
{
	union power_supply_propval pval = {
		0,
	};

	if (charger->bc12_active) {
		*pdata = charger->wired_power_max;
		return 0;
	}

	power_supply_get_property(charger->wls_psy_info->wireless_psy,
				  POWER_SUPPLY_PROP_ONLINE, &pval);
	if (pval.intval)
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_POWER_MAX, pdata);
	return 0;
}

static int
business_charger_get_quick_charge_type(struct business_charger *charger,
				       int *pdata)
{
	int online = 0;

	if (charger->bc12_active) {
		*pdata = charger->wired_qucik_charge_type;
		return 0;
	}

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
				     STRATEGY_STATUS_TYPE_ONLINE, &online);
	if (online)
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_QC_TYPE, pdata);
	return 0;
}

static int business_charger_partition_set_status(int val)
{
	int ret = 0;
	charger_partition_info_2 info_2 = { .eu_mode = 0,
					    .test = 0,
					    .reserved = 0 };

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_2,
				      sizeof(charger_partition_info_2));
	if (ret < 0) {
		mca_log_err("failed to alloc\n");
		return -1;
	}

	info_2.eu_mode = val;
	info_2.test = 0x34567890;
	info_2.reserved = 0;
	ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL,
				      CHARGER_PARTITION_INFO_2, (void *)&info_2,
				      sizeof(charger_partition_info_2));
	if (ret < 0) {
		mca_log_err("failed to write\n");
		ret = charger_partition_dealloc(
			CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2,
			sizeof(charger_partition_info_2));
		if (ret < 0) {
			mca_log_err("failed to dealloc\n");
			return -1;
		}
		return -1;
	}
	mca_log_info("ret: %d, info_2.eu_mode: %u\n", ret, info_2.eu_mode);

	ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL,
					CHARGER_PARTITION_INFO_2,
					sizeof(charger_partition_info_2));
	if (ret < 0) {
		mca_log_err("failed to dealloc\n");
		return -1;
	}

	return ret;
}

struct business_debug_ctrl_cmd {
	const char *name;
	int argc;
};

static const struct business_debug_ctrl_cmd g_debug_ctrl[] = {
	{ "double85", 1 },
	{ "remove_temp_limit", 1 },
	{ "soc_limit", 2 },
	{ "memory_test", 1 },
};

static void business_charger_process_debug_ctrl_input(const char *buf)
{
	char name[20] = { 0 };
	int v1 = 0, v2 = 0;
	int packed;

	mca_log_err("debug_ctrl input: %s\n", buf);

	if (sscanf(buf, "%19s", name) != 1) {
		mca_log_err("invalid input\n");
		return;
	}

	if (!strncmp(name, "double85", strnlen("double85", sizeof(name)))) {
		if (sscanf(buf, "%19s %d", name, &v1) != 2) {
			mca_log_err("invalid input\n");
			return;
		}
		if (v1 != g_last_double85) {
			mca_event_block_notify(MCA_EVENT_TYPE_SUBPMIC_INFO,
					       MCA_EVENT_DEBUG_CTRL_DOUBLE85,
					       &v1);
			charger_partition_write_double85(v1);
		}
	} else if (!strncmp(name, "remove_temp_limit",
			    strnlen("remove_temp_limit", sizeof(name)))) {
		if (sscanf(buf, "%19s %d", name, &v1) != 2) {
			mca_log_err("invalid input\n");
			return;
		}
		if (v1 != g_last_remove_temp_limit) {
			mca_event_block_notify(
				MCA_EVENT_TYPE_SUBPMIC_INFO,
				MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT, &v1);
			charger_partition_write_remove_temp_limit(v1);
		}
	} else if (!strncmp(name, "soc_limit", strnlen("soc_limit", sizeof(name)))) {
		if (sscanf(buf, "%19s %d %d", name, &v1, &v2) != 3) {
			mca_log_err("invalid input\n");
			return;
		}
		if (v1 < 0x65 && v2 >= 0 && v2 < 0x65) {
			if (v1 != g_last_soc_limit_hi ||
			    v2 != g_last_soc_limit_lo) {
				packed = (v1 << 8) | v2;
				mca_event_block_notify(
					MCA_EVENT_TYPE_SUBPMIC_INFO,
					MCA_EVENT_DEBUG_CTRL_SOC_LIMIT,
					&packed);
				charger_partition_write_soc_limit(packed);
			}
		} else {
			mca_log_err("invalid soc value: %d %d\n", v1, v2);
		}
	} else if (!strncmp(name, "memory_test", strnlen("memory_test", sizeof(name)))) {
		if (sscanf(buf, "%19s %d", name, &v1) != 2) {
			mca_log_err("invalid input\n");
			return;
		}
		if (v1 != g_last_memory_test) {
			mca_event_block_notify(MCA_EVENT_TYPE_SUBPMIC_INFO,
					       MCA_EVENT_DEBUG_CTRL_MEMORY_TEST,
					       &v1);
			charger_partition_write_memory_test(v1);
		}
	} else {
		mca_log_err("invalid name\n");
	}
}

static ssize_t bussiness_charger_sysfs_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info = NULL;
	struct business_charger *charger =
		(struct business_charger *)dev_get_drvdata(dev);

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  bussiness_charger_sysfs_field_tbl,
					  BUSSINESS_CHARGER_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case BUSSINESS_CHARGER_PROP_IS_EU_MODEL:
		(void)sscanf(buf, "%d", &charger->is_eu_model);
		mca_log_info("is_eu_model set: %d\n", charger->is_eu_model);
		(void)business_charger_partition_set_status(
			charger->is_eu_model);
		business_charger_process_is_eu_model(charger,
						     charger->is_eu_model);
		break;
	case BUSSINESS_CHARGER_PROP_PLATE_SHOCK:
		(void)sscanf(buf, "%d", &charger->plate_shock);
		business_charger_process_plate_shock(charger,
						     charger->plate_shock);
		break;
	case BUSSINESS_CHARGER_PROP_START_QUICK_REVCHG:
		(void)sscanf(buf, "%d", &charger->start_quick_revchg);
		business_charger_process_start_quick_revchg(
			charger, charger->start_quick_revchg);
		break;
	case BUSSINESS_CHARGER_PROP_REVCHG_BCL:
		(void)sscanf(buf, "%d", &charger->revchg_bcl);
		mca_log_info("revchg_bcl set: %d\n", charger->revchg_bcl);
		business_charger_process_set_revchg_bcl(charger,
							charger->revchg_bcl);
		break;
	case BUSSINESS_CHARGER_PROP_HANDLE_STATE:
		(void)sscanf(buf, "%d", &charger->handle_state);
		business_charger_process_handle_logic(charger);
		break;
	case BUSSINESS_CHARGER_PROP_STOP_HANDLE_CHARGE:
		(void)sscanf(buf, "%d", &charger->stop_handle_charge);
		business_charger_process_handle_logic(charger);
		break;
	case BUSSINESS_CHARGER_PROP_DEBUG_CTRL:
		business_charger_process_debug_ctrl_input(buf);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t bussiness_charger_sysfs_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	struct business_charger *charger =
		(struct business_charger *)dev_get_drvdata(dev);
	ssize_t count = 0;
	int value;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  bussiness_charger_sysfs_field_tbl,
					  BUSSINESS_CHARGER_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case BUSSINESS_CHARGER_PROP_REAL_TYPE:
		if (charger)
			count = snprintf(buf, PAGE_SIZE, "%s\n",
					 get_usb_type_name(charger->real_type));
		break;
	case BUSSINESS_CHARGER_PROP_POWER_MAX:
		business_charger_get_power_max(charger, &value);
		mca_log_info("power_max: %d\n", value);
		count = snprintf(buf, PAGE_SIZE, "%d\n", value);
		break;
	case BUSSINESS_CHARGER_PROP_QUICK_CHARGE_TYPE:
		business_charger_get_quick_charge_type(charger, &value);
		count = snprintf(buf, PAGE_SIZE, "%d\n", value);
		break;
	case BUSSINESS_CHARGER_PROP_IS_EU_MODEL:
		if (charger)
			count = snprintf(buf, PAGE_SIZE, "%d\n",
					 charger->is_eu_model);
		break;
	case BUSSINESS_CHARGER_PROP_PLATE_SHOCK:
		if (charger)
			count = snprintf(buf, PAGE_SIZE, "%d\n",
					 charger->plate_shock);
		break;
	case BUSSINESS_CHARGER_PROP_START_QUICK_REVCHG:
		if (charger)
			count = snprintf(buf, PAGE_SIZE, "%d\n",
					 charger->start_quick_revchg);
		break;
	case BUSSINESS_CHARGER_PROP_REVCHG_BCL:
		if (charger)
			count = snprintf(buf, PAGE_SIZE, "%d\n",
					 charger->revchg_bcl);
		break;
	case BUSSINESS_CHARGER_PROP_HANDLE_STATE:
		if (charger)
			count = snprintf(buf, PAGE_SIZE, "%d\n",
					 charger->handle_state);
		break;
	case BUSSINESS_CHARGER_PROP_STOP_HANDLE_CHARGE:
		if (charger)
			count = snprintf(buf, PAGE_SIZE, "%d\n",
					 charger->stop_handle_charge);
		break;
	case BUSSINESS_CHARGER_PROP_DEBUG_CTRL:
		count += sprintf(buf + count, "%s %d\n", g_debug_ctrl[0].name,
				 g_last_double85);
		count += sprintf(buf + count, "%s %d\n", g_debug_ctrl[1].name,
				 g_last_remove_temp_limit);
		count += sprintf(buf + count, "%s %d %d\n", g_debug_ctrl[2].name,
				 g_last_soc_limit_hi, g_last_soc_limit_lo);
		count += sprintf(buf + count, "%s %d\n", g_debug_ctrl[3].name,
				 g_last_memory_test);
		break;
	default:
		break;
	}
	return count;
}

static int business_charger_sysfs_create_group(struct business_charger *charger)
{
	mca_sysfs_init_attrs(bussiness_charger_sysfs_attrs,
			     bussiness_charger_sysfs_field_tbl,
			     BUSSINESS_CHARGER_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_1, "charger_common",
					   charger->dev,
					   &bussiness_charger_sysfs_attr_group);
}

static void business_charger_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group(SYSFS_DEV_1, "charge_common", dev,
				    &bussiness_charger_sysfs_attr_group);
}

static int business_charger_event_thread(void *args)
{
	struct business_charger *charger = args;

	business_charger_process_online_change(charger, MCA_EVENT_USB_CONNECT);
	business_charger_process_wireless_online_change(
		charger, MCA_EVENT_WIRELESS_CONNECT);

	while (!kthread_should_stop()) {
		wait_event_interruptible(charger->wait_que,
					 (charger->thread_active ==
					  BUSINESS_CHARGER_THREAD_ACTIVE) || kthread_should_stop());
		if (kthread_should_stop())
			break;
		charger->thread_active = 0;
		business_charger_process_event(charger);
	};
	return 0;
}

static ssize_t business_debugfs_show(void *priv_data, char *buf)
{
	struct mca_debugfs_attr_data *attr_data =
		(struct mca_debugfs_attr_data *)priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct business_charger *charger =
		(struct business_charger *)attr_data->private;
	size_t count = 0;

	if (!charger || !attr_info) {
		mca_log_err("null pointer store\n");
		return count;
	}

	switch (attr_info->debugfs_attr_name) {
	case BUSINESS_CHARGER_DEBUG_PROP_DAM_TEST:
		count = scnprintf(buf, PAGE_SIZE, "%d\n",
				  charger->dam_test_flag);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t business_debugfs_store(void *priv_data, const char *buf,
				      size_t count)
{
	struct mca_debugfs_attr_data *attr_data =
		(struct mca_debugfs_attr_data *)priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct business_charger *charger =
		(struct business_charger *)attr_data->private;
	int val;

	if (kstrtoint(buf, 16, &val))
		return -EINVAL;

	switch (attr_info->debugfs_attr_name) {
	case BUSINESS_CHARGER_DEBUG_PROP_DAM_TEST:
		charger->dam_test_flag = val;
		if (charger->dam_test_flag) {
			mca_log_info("relax wake_lock\n");
			__pm_relax(charger->online_wake_lock);
		} else if (charger->bc12_active) {
			__pm_stay_awake(charger->online_wake_lock);
			mca_log_info("stay wake_lock\n");
		}
		break;
	default:
		break;
	}

	return count;
}

static struct mca_debugfs_attr_info g_business_charge_debugfs_field_tbl[] = {
	mca_debugfs_attr(business_debugfs, 0664,
			 BUSINESS_CHARGER_DEBUG_PROP_DAM_TEST, dam_test),
};
#define BUSINESS_CHARGER_DEBUGFS_ATTRS_SIZE \
	ARRAY_SIZE(g_business_charge_debugfs_field_tbl)

static int business_charge_dump_log_head(void *data, char *buf, int size)
{
	return snprintf(
		buf, size,
		"Online ChargeType ChargeEnable ChargeMode Vbus   Ibus   BTB_Status Batt_Auth ");
}

static int business_charge_dump_log_context(void *data, char *buf, int size)
{
	struct business_charger *charger = (struct business_charger *)data;
	int online = 0;
	int enable = 0;
	int chg_type = 0;
	int ibus;
	int vbus;
	int mode = 0;
	int temp = 0;

	if (!data)
		return snprintf(buf, size,
				"%-7d%-11d0x%-11x%-11d%-7d%-7d%-11d%-10d", -1,
				-1, -1, -1, -1, -1, -1, -1);

	if (charger->bc12_active) {
		online = 1;
		chg_type = charger->real_type;
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					     STRATEGY_STATUS_TYPE_MODE, &mode);
	} else if (charger->wls_active) {
		online = 2;
		chg_type = charger->wls_adapter_type;
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
					     STRATEGY_STATUS_TYPE_MODE, &mode);
	}

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					   STRATEGY_STATUS_TYPE_ENABLE, &temp);
	enable |= temp;
	temp = 0;
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					   STRATEGY_STATUS_TYPE_ENABLE, &temp);
	enable |= temp << 4;
	temp = 0;
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
					   STRATEGY_STATUS_TYPE_ENABLE, &temp);
	enable |= temp << 14;

	if (mode) {
		if (charger->bc12_active) {
			(void)mca_strategy_func_get_status(
				STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				STRATEGY_STATUS_TYPE_VBUS, &vbus);
			(void)mca_strategy_func_get_status(
				STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				STRATEGY_STATUS_TYPE_IBUS, &ibus);
		} else {
			(void)mca_strategy_func_get_status(
				STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
				STRATEGY_STATUS_TYPE_VBUS, &vbus);
			(void)mca_strategy_func_get_status(
				STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
				STRATEGY_STATUS_TYPE_IBUS, &ibus);
		}
	} else {
		(void)platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER,
							      &vbus);
		(void)platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER,
							      &ibus);
		vbus /= 1000;
		ibus /= 1000;
	}

	return snprintf(buf, size, "%-7d%-11d0x%-11x%-11d%-7d%-7d%-11d%-10d",
			online, chg_type, enable, mode, vbus, ibus,
			charger->abnormal_info.batt_missing,
			!charger->abnormal_info.batt_auth_failed);
}

static struct mca_log_charge_log_ops g_business_charger_log_ops = {
	.dump_log_head = business_charge_dump_log_head,
	.dump_log_context = business_charge_dump_log_context,
};

static int business_charger_probe(struct platform_device *pdev)
{
	struct business_charger *charger;
	static int probe_cnt;
	int rc = 0;

	mca_log_err("probe begin cnt %d\n", ++probe_cnt);
	charger = devm_kzalloc(&pdev->dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	charger->dev = &pdev->dev;
	platform_set_drvdata(pdev, charger);
	rc = business_charger_parse_dt(charger);
	if (rc < 0) {
		mca_log_err(" Couldn't parse device tree rc=%d\n", rc);
		return rc;
	}

	charger->usb_psy_info = business_charger_usb_psy_init(charger->dev);
	if (!charger->usb_psy_info) {
		mca_log_err("Couldn't init usb psy rc=%d\n", rc);
		return -EINVAL;
	}

	charger->wls_psy_info =
		business_charger_wireless_psy_init(charger->dev);
	if (!charger->wls_psy_info) {
		mca_log_err("Couldn't init wireless psy rc=%d\n", rc);
		return -EINVAL;
	}

	INIT_LIST_HEAD(&charger->header);
	spin_lock_init(&charger->list_lock);
	init_waitqueue_head(&charger->wait_que);
	charger->usb_psy_info->charge_enable = 1;
	charger->abnormal_info.asuint32 = 0;
	charger->abnormal_info.batt_auth_failed = 1;
	INIT_DELAYED_WORK(&charger->delay_report_status_work,
			  business_charger_wireless_report_status_work);
	INIT_DELAYED_WORK(&charger->delay_enable_rx_work,
			  business_charger_wireless_delay_enable_rx_work);
	INIT_DELAYED_WORK(&charger->reset_rx_work,
			  business_charger_wireless_reset_rx_work);
	INIT_DELAYED_WORK(&charger->report_quick_charge_type_work,
			  business_charger_report_quick_charge_type_work);
	INIT_DELAYED_WORK(&charger->wireless_rerun_usb_sns_work,
			  business_charger_wireless_rerun_usb_sns_work);
	INIT_DELAYED_WORK(&charger->pmic_init_done_notify_work,
			  business_charger_pmic_init_done_notify_work);

	charger->connect_nb.notifier_call =
		business_charger_notifier_connect_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					     &charger->connect_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail2;
	}

	charger->type_change_nb.notifier_call =
		business_charger_notifier_type_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGE_TYPE,
					     &charger->type_change_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail1;
	}

	charger->hw_info_nb.notifier_call = business_charger_notifier_hw_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_HW_INFO,
					     &charger->hw_info_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail3;
	}

	charger->chg_sts_nb.notifier_call =
		business_charger_notifier_chg_sts_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_CHARGE_STATUS,
					     &charger->chg_sts_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail4;
	}

	charger->cp_info_nb.notifier_call =
		business_charger_notifier_cp_info_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_CP_INFO,
					     &charger->cp_info_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail5;
	}

	charger->debug_nb.notifier_call = business_charger_notifier_debug_cb;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_SUBPMIC_INFO,
					     &charger->debug_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		goto reg_notify_fail6;
	}

	schedule_delayed_work(&charger->wireless_rerun_usb_sns_work,
			      msecs_to_jiffies(2500));
	schedule_delayed_work(&charger->pmic_init_done_notify_work, 0);

	charger->online_wake_lock =
		wakeup_source_register(charger->dev, "charger_wakelock");
	if (!charger->online_wake_lock)
		mca_log_err("reg wakelock failed\n");
	charger->event_thread = kthread_run(business_charger_event_thread, charger,
		    "charger_event_thread");
	if (IS_ERR(charger->event_thread)) {
		rc = PTR_ERR(charger->event_thread);
		charger->event_thread = NULL;
		WRITE_ONCE(charger->stopping, true);
		goto reg_notify_fail7;
	}
	if (business_charger_sysfs_create_group(charger))
		mca_log_err("create sysfs failed\n");
	mca_debugfs_create_group("business_charger",
				 g_business_charge_debugfs_field_tbl,
				 BUSINESS_CHARGER_DEBUGFS_ATTRS_SIZE, charger);
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_BUSINESS_CHG,
				    &g_business_charger_log_ops, charger);
	mca_log_err("probe success\n");
	return 0;

reg_notify_fail7:
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_SUBPMIC_INFO, &charger->debug_nb);
reg_notify_fail6:
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_CP_INFO,
						&charger->cp_info_nb);
reg_notify_fail5:
	(void)mca_event_block_notify_unregister(MCA_EVENT_CHARGE_STATUS,
						&charger->chg_sts_nb);
reg_notify_fail4:
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_HW_INFO,
						&charger->hw_info_nb);
reg_notify_fail3:
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGE_TYPE,
						&charger->type_change_nb);
reg_notify_fail1:
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
						&charger->connect_nb);
reg_notify_fail2:
	if (charger->online_wake_lock)
		wakeup_source_unregister(charger->online_wake_lock);
	business_charger_wireless_psy_deinit(charger->wls_psy_info);
	WRITE_ONCE(charger->stopping, true);
	cancel_delayed_work_sync(&charger->delay_report_status_work);
	cancel_delayed_work_sync(&charger->delay_enable_rx_work);
	cancel_delayed_work_sync(&charger->reset_rx_work);
	cancel_delayed_work_sync(&charger->report_quick_charge_type_work);
	cancel_delayed_work_sync(&charger->wireless_rerun_usb_sns_work);
	cancel_delayed_work_sync(&charger->pmic_init_done_notify_work);
	business_charger_usb_psy_deinit(charger->usb_psy_info);
	mca_log_info("%s !!\n",
		     rc == -EPROBE_DEFER ? "Over probe cnt max" : "OK");

	return rc;
}

static int business_charger_remove(struct platform_device *pdev)
{
	struct business_charger *charger = platform_get_drvdata(pdev);
	struct charger_event_lis_node *event, *next;

	WRITE_ONCE(charger->stopping, true);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT, &charger->connect_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGE_TYPE, &charger->type_change_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_HW_INFO, &charger->hw_info_nb);
	mca_event_block_notify_unregister(MCA_EVENT_CHARGE_STATUS, &charger->chg_sts_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CP_INFO, &charger->cp_info_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_SUBPMIC_INFO, &charger->debug_nb);
	if (charger->event_thread)
		kthread_stop(charger->event_thread);
	cancel_delayed_work_sync(&charger->delay_report_status_work);
	cancel_delayed_work_sync(&charger->delay_enable_rx_work);
	cancel_delayed_work_sync(&charger->reset_rx_work);
	cancel_delayed_work_sync(&charger->report_quick_charge_type_work);
	cancel_delayed_work_sync(&charger->wireless_rerun_usb_sns_work);
	cancel_delayed_work_sync(&charger->pmic_init_done_notify_work);
	cancel_delayed_work_sync(&charger->delay_report_status_work);
	cancel_delayed_work_sync(&charger->delay_enable_rx_work);
	cancel_delayed_work_sync(&charger->reset_rx_work);
	cancel_delayed_work_sync(&charger->report_quick_charge_type_work);
	cancel_delayed_work_sync(&charger->wireless_rerun_usb_sns_work);
	cancel_delayed_work_sync(&charger->pmic_init_done_notify_work);
	mca_log_charge_log_unregister(charger);
	mca_debugfs_remove_groups(charger);
	business_charger_sysfs_remove_group(&pdev->dev);
	business_charger_usb_psy_deinit(charger->usb_psy_info);
	business_charger_wireless_psy_deinit(charger->wls_psy_info);
	if (charger->online_wake_lock)
		wakeup_source_unregister(charger->online_wake_lock);
	list_for_each_entry_safe(event, next, &charger->header, lnode) {
		list_del(&event->lnode);
		kfree(event->data);
		kfree(event);
	}
	return 0;
}

static void business_charger_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{ .compatible = "mca,business_charger" },
	{},
};

static struct platform_driver business_charger_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "business_charger",
		.of_match_table = match_table,
	},
	.probe = business_charger_probe,
	.remove = business_charger_remove,
	.shutdown = business_charger_shutdown,
};

static int __init business_charger_init(void)
{
	return platform_driver_register(&business_charger_driver);
}
module_init(business_charger_init);

static void __exit business_charger_exit(void)
{
	platform_driver_unregister(&business_charger_driver);
}
module_exit(business_charger_exit);

MODULE_DESCRIPTION("business charger core");
MODULE_AUTHOR("getian@xiaomi.com");
MODULE_LICENSE("GPL v2");

MODULE_DEVICE_TABLE(of, match_table);
