// SPDX-License-Identifier: GPL-2.0
/*
 *mca_strategy_fg.c
 *
 * mca fuelgauge strategy driver
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
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_voter.h>
#include <mca/common/mca_smem.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_loadsw_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/common/mca_workqueue.h>
#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/timekeeping.h>
#include "inc/strategy_fg.h"
#include "inc/mca_soc_smooth.h"
#include "inc/xm_battery_auth.h"
#include <mca/common/mca_hwid.h>
#include "hwid.h"
#include <mca/shared_memory/charger_partition_class.h>

#ifndef PROBE_CNT_MAX
#define PROBE_CNT_MAX 10
#endif

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_strategy_fg"
#endif

#define STRATEGY_FG_WORK_INTERVAL_FAST 1000
#define STRATEGY_FG_WORK_INTERVAL_NORMAL 5000
#define STRATEGY_FG_WORK_INTERVAL_SCREEN_ON 10000
#define STRATEGY_FG_WORK_INTERVAL_SCREEN_OFF 30000
#define STRATEGY_FG_WORK_INTERVAL_LOW_VOLT 5000
#define STRATEGY_FG_WORK_INTERVAL_LOW_TEMP 5000
#define STRATEGY_FG_WORK_INTERVAL_LOW_TEMP_FAST 2000
#define STRATEGY_FG_WORK_INTERVAL_DTPT 5000
#define STRATEGY_FG_RESUME_UPDATE_TIME_MS 3000
#define STRATEGY_FG_REPORT_FULL_RSOC 9700
#define STRATEGY_FG_SOC_PROPORTION 97
#define STRATEGY_FG_SOC_PROPORTION_C 96
#define STRATEGY_FG_ADAPT_POWER 0
#define STRATEGY_FG_SUPPORT_DTPT 0
#define STRATEGY_FG_TERMINATED_BY_CP 0
#define STRATEGY_FG_SUPPORT_FL4P0 0
#define CHARGE_SLOWLY_SOC_MAX 80
#define CHARGE_SLOWLY_POWER_MIN_W 32
#define CHARGE_SLOWLY_POWER_MID_MW 67000
#define CHARGE_SLOWLY_POWER_HIGH_MW 100000
#define CHARGE_SLOWLY_RM_THR_LOW 200
#define CHARGE_SLOWLY_RM_THR_MID 420
#define CHARGE_SLOWLY_RM_THR_HIGH 500
#define CHARGE_SLOWLY_CHECK_PERIOD_S 599
#define CHARGE_SLOWLY_CAPACITY_MARGIN 100
#define CHARGE_SLOWLY_DFX_PARA_NUM 5
#define NON_STD_CHARGER_PLUGIN_TIME_S 60

#define JEITA_COOL_THR_DEGREE 150
#define EXTREME_HIGH_DEGREE 1000
#define STRATEGY_FG_DEFAULT_VTERM 4400
#define STRATEGY_FG_DEFAULT_HIGH_TEMP_VTERM 4400
#define STRATEGY_FG_DEFAULT_ITERM 250
#define STRATEGY_FG_ERROR_FAKE_SOC 15
#define STRATEGY_FG_ERROR_FAKE_TEMP 250
#define STRATEGY_FG_ERROR_FAKE_CURR -500000
#define STRATEGY_FG_ERROR_FAKE_VOLT 3700
#define STRATEGY_FG_UPDATE_PERIOD_NONE -1
#define BATT_TEMP_COLD_TH 0
#define BATT_LOW_VOLT_HY 200
#define BATT_LOW_VOLT_SW_HY 50
#define DEFAULT_DESIGN_CAPACITY 5000
#define FG_FORCE_REPORT_FULL_TIMES 2
#define STRATEGY_FG_ABNORMAL_DFX_DELAY 17500

void onewire_check_auth_pass(bool *auth_pass)
{
	*auth_pass = false;
}

void onewire_check_chip_ok(bool *chip_ok)
{
	*chip_ok = false;
}

int onewire_get_shutdown_uisoc(int *uisoc)
{
	*uisoc = STRATEGY_FG_ERROR_FAKE_SOC;
	return -1;
}

int onewire_set_shutdown_uisoc(int uisoc)
{
	return -1;
}

static void strategy_fg_init_voter(struct strategy_fg *fg);
static int strategy_fg_ops_get_curr(void *data, int *curr);
static int strategy_fg_ops_get_rsoc(void *data, int *rsoc);
static void strategy_fg_set_fake_soc(struct strategy_fg *info, int val);
static void strategy_fg_set_fake_temp(struct strategy_fg *info, int val);

static ssize_t strategy_fg_sysfs_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf);
static ssize_t strategy_fg_sysfs_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count);
static int strategy_fg_check_battery_adapt_power(void *data, int *match);
static void mca_strategy_get_manufacturing_date(struct strategy_fg *fg,
						char *date);
static void mca_strategy_get_first_usage_date(struct strategy_fg *fg,
					      char *date);
static void mca_strategy_set_first_usage_date(struct strategy_fg *fg,
					      const char *date);

static int strategy_fg_ops_get_soc_decimal(void *data, int *soc_decimal,
					   int *rate);
static int strategy_fg_ops_get_fastcharge(void *data);
static int strategy_fg_get_dod_count(void *data, int *value);
static int
strategy_fg_check_extreme_cold_temp_compensation(struct strategy_fg *fg,
						 int real_temp);
static int strategy_fg_get_parallel_curr(struct strategy_fg *fg, int *curr);
static int strategy_fg_get_parallel_rsoc(struct strategy_fg *fg, int *rsoc);
static int strategy_fg_get_soh_new(struct strategy_fg *fg, int *soh_new);
static unsigned long strategy_fg_get_calc_rvalue(struct strategy_fg *fg);

static struct mca_sysfs_attr_info strategy_fg_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_MAIN_AUTH,
			  authentic),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_SLAVE_AUTH,
			  slave_authentic),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_BATT_INDEX,
			  verify_slave_flag),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_VERIFY_DIGEST,
			  verify_digest),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664,
			  FG_PROP_BATT_POWER_MATCH, batt_power_match),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_FAST_CHARGE,
			  fast_charge),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_SOC_DECIMAL,
			  soc_decimal),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440,
			  FG_PROP_SOC_DECIMAL_RATE, soc_decimal_rate),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_FAKE_SOC,
			  fake_soc),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_FAKE_TEMP,
			  fake_temp),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_BATTERY_NUM,
			  battery_num),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_UPDATE_PERIOD,
			  update_period),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_PACK_VOLTAGE,
			  pack_vbat),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_DOD_COUNT,
			  dod_count),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_ENABLE_ROLLBACK,
			  enable_rollback),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_SOH_NEW,
			  soh_new),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440,
			  FG_PROP_MASTER_SELF_EQUAL_COUNT,
			  master_self_equal_count),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440,
			  FG_PROP_SLAVE_SELF_EQUAL_COUNT,
			  slave_self_equal_count),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664,
			  FG_PROP_SELF_EQUAL_MAX_COUNT, self_equal_max_count),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_PACK_TEMP,
			  pack_tbat),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_RAW_SOC,
			  raw_soc),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_CALC_RVALUE,
			  calc_rvalue),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_MANUFACTURING_DATE,
			  manufacturing_date),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0640, FG_PROP_FIRST_USAGE_DATE,
			  first_usage_date),
};

#define FG_AUTH_ATTRS_SIZE ARRAY_SIZE(strategy_fg_sysfs_field_tbl)

static struct attribute *strategy_fg_sysfs_attrs[FG_AUTH_ATTRS_SIZE + 1];

static const struct attribute_group strategy_fg_sysfs_attr_group = {
	.attrs = strategy_fg_sysfs_attrs,
};

static int strategy_fg_auth_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(strategy_fg_sysfs_attrs,
			     strategy_fg_sysfs_field_tbl,
			     FG_AUTH_ATTRS_SIZE);
	return mca_sysfs_create_link_group("fuelgauge", "strategy_fg", dev,
					   &strategy_fg_sysfs_attr_group);
}

static void strategy_fg_auth_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group("fuelgauge", "strategy_fg", dev,
				    &strategy_fg_sysfs_attr_group);
}

static void strategy_fg_set_fake_soc(struct strategy_fg *info, int val)
{
	mca_log_err("set fake_soc: %d\n", val);
	if (val != info->fake_soc) {
		if (val == STRATEGY_FG_FAKE_SOC_NONE ||
		    (val >= 0 && val <= 100)) {
			info->fake_soc = val;
			mca_cancel_delayed_work_sync(&info->monitor_work);
			mca_queue_delayed_work(&info->monitor_work, 0);

			mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
					       MCA_EVENT_BATTERY_STS_CHANGE,
					       NULL);
		}
	}
}

static void strategy_fg_set_fake_temp(struct strategy_fg *info, int val)
{
	mca_log_err("set fake_temp: %d\n", val);
	if (val != info->fake_temp) {
		info->fake_temp = val;
		platform_fg_ops_set_temp(FG_IC_MASTER, val);
		if (info->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX)
			platform_fg_ops_set_temp(FG_IC_SLAVE, val);

		mca_cancel_delayed_work_sync(&info->monitor_work);
		mca_queue_delayed_work(&info->monitor_work, 0);

		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
				       MCA_EVENT_BATTERY_STS_CHANGE, NULL);
	}
}

static ssize_t strategy_fg_sysfs_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct strategy_fg *info = dev_get_drvdata(dev);
	if (info && READ_ONCE(info->stopping))
		return -ESHUTDOWN;

	struct mca_sysfs_attr_info *attr_info;
	int val;
	int match = 0;

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  strategy_fg_sysfs_field_tbl,
					  FG_AUTH_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case FG_PROP_MAIN_AUTH:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		platform_fg_ops_set_authentic(FG_IC_MASTER, val);
		info->batt_auth = val;
		strategy_fg_check_battery_adapt_power(info, &match);
		if (val && info->cfg.fg_type <= MCA_FG_TYPE_SINGLE_NUM_MAX &&
		    match)
			mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
					       MCA_EVENT_BATT_AUTH_PASS, NULL);
		break;
	case FG_PROP_SLAVE_AUTH:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		platform_fg_ops_set_authentic(FG_IC_SLAVE, val);
		info->batt_slave_auth = val;
		strategy_fg_check_battery_adapt_power(info, &match);
		if (info->batt_auth && info->batt_slave_auth && match)
			mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
					       MCA_EVENT_BATT_AUTH_PASS, NULL);
		break;
	case FG_PROP_BATT_INDEX:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		info->batt_index = val;
		break;
	case FG_PROP_VERIFY_DIGEST:
		platform_fg_ops_set_verify_digest(info->batt_index,
						  (char *)buf);
		break;
	case FG_PROP_BATT_POWER_MATCH:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		info->fake_bap_match = val;
		mca_log_info("set fake_bap_match: %d\n", val);
		break;
	case FG_PROP_FAKE_SOC:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		strategy_fg_set_fake_soc(info, val);
		break;
	case FG_PROP_FAKE_TEMP:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		strategy_fg_set_fake_temp(info, val);
		break;
	case FG_PROP_UPDATE_PERIOD:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		mca_log_err("set fg update period: %d\n", val);
		if (val != info->update_period) {
			info->update_period = val;
			mca_cancel_delayed_work_sync(&info->monitor_work);
			mca_queue_delayed_work(&info->monitor_work, 0);

			mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
					       MCA_EVENT_BATTERY_STS_CHANGE,
					       NULL);
		}
		break;
	case FG_PROP_DOD_COUNT:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		mca_log_err("set fake_dod_count: %d\n", val);
		info->fake_dod_count = val;
		break;
	case FG_PROP_ENABLE_ROLLBACK:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		mca_log_err("set enable_rollback: %d\n", val);
		info->enable_rollback = !!val;
		break;
	case FG_PROP_SELF_EQUAL_MAX_COUNT:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		mca_log_err("set self_equal_count_max: %d\n", val);
		info->self_equal_max_count = val;
		break;
	case FG_PROP_FIRST_USAGE_DATE:
		mca_strategy_set_first_usage_date(info, buf);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t strategy_fg_sysfs_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	ssize_t count = 0;
	struct mca_sysfs_attr_info *attr_info;
	struct strategy_fg *info = dev_get_drvdata(dev);
	if (info && READ_ONCE(info->stopping))
		return -ESHUTDOWN;

	int val = 0;
	int temp = 0;
	unsigned long rvalue;

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  strategy_fg_sysfs_field_tbl,
					  FG_AUTH_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case FG_PROP_MAIN_AUTH:
		platform_fg_ops_get_authentic(FG_IC_MASTER, &val);
		count = scnprintf(buf, PAGE_SIZE, "%x\n", val);
		break;
	case FG_PROP_SLAVE_AUTH:
		platform_fg_ops_get_authentic(FG_IC_SLAVE, &val);
		count = scnprintf(buf, PAGE_SIZE, "%x\n", val);
		break;
	case FG_PROP_BATT_INDEX:
		count = scnprintf(buf, PAGE_SIZE, "%x\n", info->batt_index);
		break;
	case FG_PROP_VERIFY_DIGEST:
		count = platform_fg_ops_get_verify_digest(info->batt_index,
							  (char *)buf);
		break;
	case FG_PROP_BATT_POWER_MATCH:
		strategy_fg_check_battery_adapt_power(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%x\n", val);
		break;
	case FG_PROP_FAST_CHARGE:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->fast_charge);
		break;
	case FG_PROP_SOC_DECIMAL:
		strategy_fg_ops_get_soc_decimal(info, &val, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_PROP_SOC_DECIMAL_RATE:
		strategy_fg_ops_get_soc_decimal(info, &val, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case FG_PROP_FAKE_SOC:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->fake_soc);
		break;
	case FG_PROP_FAKE_TEMP:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->fake_temp);
		break;
	case FG_PROP_BATTERY_NUM:
		count = scnprintf(
			buf, PAGE_SIZE, "%d\n",
			info->cfg.fg_type == MCA_FG_TYPE_PARALLEL ? 1 : 0);
		break;
	case FG_PROP_UPDATE_PERIOD:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->update_period);
		break;
	case FG_PROP_PACK_VOLTAGE:
		platform_class_buckchg_ops_get_pack_vbat(MAIN_BUCK_CHARGER,
							 &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_PROP_DOD_COUNT:
		strategy_fg_get_dod_count(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_PROP_ENABLE_ROLLBACK:
		count = scnprintf(buf, PAGE_SIZE, "%d\n",
				  info->enable_rollback);
		break;
	case FG_PROP_SOH_NEW:
		strategy_fg_get_soh_new(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_PROP_MASTER_SELF_EQUAL_COUNT:
		count = scnprintf(buf, PAGE_SIZE, "%d\n",
				  info->self_equal_count[FG_IC_MASTER]);
		break;
	case FG_PROP_SLAVE_SELF_EQUAL_COUNT:
		count = scnprintf(buf, PAGE_SIZE, "%d\n",
				  info->self_equal_count[FG_IC_SLAVE]);
		break;
	case FG_PROP_SELF_EQUAL_MAX_COUNT:
		count = scnprintf(buf, PAGE_SIZE, "%d\n",
				  info->self_equal_max_count);
		break;
	case FG_PROP_PACK_TEMP:
		platform_class_buckchg_ops_get_pack_tbat(MAIN_BUCK_CHARGER,
							 &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_PROP_RAW_SOC:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->batt_raw_soc);
		break;
	case FG_PROP_CALC_RVALUE:
		rvalue = strategy_fg_get_calc_rvalue(info);
		count = scnprintf(buf, PAGE_SIZE, "%lu\n", rvalue);
		break;
	case FG_PROP_MANUFACTURING_DATE: {
		char date_buf[16] = { 0 };

		mca_strategy_get_manufacturing_date(info, date_buf);
		count = scnprintf(buf, PAGE_SIZE, "%s\n", date_buf);
		break;
	}
	case FG_PROP_FIRST_USAGE_DATE: {
		char date_buf[16] = { 0 };

		mca_strategy_get_first_usage_date(info, date_buf);
		count = scnprintf(buf, PAGE_SIZE, "%s\n", date_buf);
		break;
	}
	default:
		break;
	}

	return count;
}

#define MEAN_CUR_NUM 5
static void strategy_fg_record_curr_mean(struct strategy_fg *fg)
{
	static int current_nums[MEAN_CUR_NUM] = { 0 };
	static int cur_num = 0;
	int sum = 0;

	if (!fg || !fg->fg_init_flag)
		return;

	current_nums[cur_num] = fg->batt_current;
	cur_num = (cur_num + 1) % MEAN_CUR_NUM;
	mca_log_debug(
		"record_i:%d, record_cur [0]= %d, [1]= %d, [2]= %d, [3]= %d, [4]= %d\n",
		cur_num, current_nums[0], current_nums[1], current_nums[2],
		current_nums[3], current_nums[4]);

	for (int i = 0; i < MEAN_CUR_NUM; i++)
		sum = sum + current_nums[i];

	fg->batt_current_mean = sum / MEAN_CUR_NUM;
	mca_log_info("mean_cur:%d\n", fg->batt_current_mean);
	return;
}

#define MEAN_VOL_NUM 5
#define DISCHARGING_MAX_EMPTY_COUNT 2
#define FAST_CHARGING_MAX_EMPTY_COUNT 10
#define NORMAL_CHARGING_MAX_EMPTY_COUNT 2
#define VABTT_EMPTY_HY 100
void strategy_fg_record_volt_mean(struct strategy_fg *fg)
{
	static int vol_i = 0, voltage_nums[MEAN_VOL_NUM] = { 0 };
	int sum = 0, cnt_i = 0;
	static int empty_count;
	int empty_count_th = DISCHARGING_MAX_EMPTY_COUNT;
	struct timespec64 ts;
	static int is_zero_speed = 0;

	if (!fg || !fg->fg_init_flag)
		return;
	if (fg->batt_voltage < 1000)
		return;

	voltage_nums[vol_i] = fg->batt_voltage;
	mca_log_debug(
		"record_i:%d record_vol [0]= %d, [1]= %d, [2]= %d, [3]= %d, [4]= %d\n",
		vol_i, voltage_nums[0], voltage_nums[1], voltage_nums[2],
		voltage_nums[3], voltage_nums[4]);
	vol_i = (vol_i + 1) % MEAN_VOL_NUM;

	for (cnt_i = 0; cnt_i < MEAN_VOL_NUM; cnt_i++) {
		if (voltage_nums[cnt_i] == 0)
			break;
		if (voltage_nums[cnt_i] > 0)
			sum += voltage_nums[cnt_i];
	}
	if (cnt_i == 0)
		fg->batt_voltage_mean = 0;
	else
		fg->batt_voltage_mean = sum / cnt_i;
	mca_log_info("mean_vol:%d\n", fg->batt_voltage_mean);

	if (fg->chg_status != POWER_SUPPLY_STATUS_CHARGING)
		empty_count_th = DISCHARGING_MAX_EMPTY_COUNT;
	else
		empty_count_th = (fg->fast_charge ?
					  FAST_CHARGING_MAX_EMPTY_COUNT :
					  NORMAL_CHARGING_MAX_EMPTY_COUNT);

	ktime_get_boottime_ts64(&ts);
	if (!is_zero_speed && fg->chg_status == POWER_SUPPLY_STATUS_CHARGING &&
	    (u64)ts.tv_sec < 120)
		get_smem_battery_info(&is_zero_speed);
	if (fg->chg_status == POWER_SUPPLY_STATUS_CHARGING && is_zero_speed &&
	    (u64)ts.tv_sec < 120) {
		mca_log_err(
			"chg_status = %d, is_zero_speed = %d, tv_sec = %llu, ignore vbatt_empty...\n",
			fg->chg_status, is_zero_speed, (u64)ts.tv_sec);
	} else {
		if (fg->batt_voltage_mean <= fg->vcutoff_sw) {
			empty_count++;
			fg->vbatt_empty =
				(empty_count > empty_count_th) ? true : false;
		} else if (fg->batt_voltage_mean >
			   (fg->vcutoff_sw + VABTT_EMPTY_HY)) {
			empty_count = 0;
			fg->vbatt_empty = false;
		}
	}

	mca_log_info("empty_count_th: %d, vbatt_empty: %d\n", empty_count_th,
		     fg->vbatt_empty);
}

static int strategy_fg_get_volt_mean(void *data, int *vol_mean)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*vol_mean = fg->batt_voltage_mean;

	return 0;
}

static int strategy_fg_get_fast_charge(struct strategy_fg *fg)
{
	int ffc = 0;
	int slave_ffc = 0;

	if (!fg || !fg->fg_init_flag)
		return 0;

	platform_fg_ops_get_fastcharge(FG_IC_MASTER, &ffc);
	if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX) {
		platform_fg_ops_get_fastcharge(FG_IC_SLAVE, &slave_ffc);
		ffc = (ffc && slave_ffc);
	}

	return ffc;
}

static int strategy_fg_get_cutoff_voltage(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	int cutoff_voltage = 0;
	int ret;

	if (fg->cfg.fg_type <= MCA_FG_TYPE_SINGLE_NUM_MAX)
		ret = platform_fg_ops_get_cutoff_voltage(FG_IC_MASTER,
							 &cutoff_voltage);
	else if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX) {
		ret = platform_fg_ops_get_cutoff_voltage(FG_IC_MASTER,
							 &cutoff_voltage);
	}

	return cutoff_voltage;
}

static int strategy_fg_set_cutoff_voltage(void *data, int value)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (fg->cfg.fg_type <= MCA_FG_TYPE_SINGLE_NUM_MAX)
		platform_fg_ops_set_cutoff_voltage(FG_IC_MASTER, value);
	else {
		platform_fg_ops_set_cutoff_voltage(FG_IC_MASTER, value);
		platform_fg_ops_set_cutoff_voltage(FG_IC_SLAVE, value);
	}

	return 0;
}

#define WEIGHT_SCALE 10
static int strategy_fg_get_dod_count(void *data, int *value)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	int count_level1 = 0, count_level2 = 0, count_level3 = 0,
	    count_lowtemp = 0;
	int ret = 0;

	if (!fg->support_dod_vcutoff)
		return 0;

	if (fg->fake_dod_count) {
		*value = fg->fake_dod_count;
		return 0;
	}
	ret = platform_fg_ops_get_dod_count(FG_IC_MASTER);
	ret |= platform_fg_ops_get_count_level1(FG_IC_MASTER, &count_level1);
	ret |= platform_fg_ops_get_count_level2(FG_IC_MASTER, &count_level2);
	ret |= platform_fg_ops_get_count_level3(FG_IC_MASTER, &count_level3);
	ret |= platform_fg_ops_get_count_lowtemp(FG_IC_MASTER, &count_lowtemp);

	*value = (fg->weight[WEIGHT_LEVEL_1] * count_level1 +
		  fg->weight[WEIGHT_LEVEL_2] * count_level2 +
		  fg->weight[WEIGHT_LEVEL_3] * count_level3) /
		 WEIGHT_SCALE;

	if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX)
		ret |= platform_fg_ops_get_dod_count(FG_IC_SLAVE);

	return 0;
}

static void strategy_fg_ops_get_single_soh_new(struct strategy_fg *fg,
					       int *soh_new)
{
	int master_ui_soh;

	platform_fg_ops_get_ui_soh(FG_IC_MASTER, &master_ui_soh);
	*soh_new = master_ui_soh;
}

static int strategy_fg_get_soh_new(struct strategy_fg *fg, int *soh_new)
{
	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		strategy_fg_ops_get_single_soh_new(fg, soh_new);
		break;
	case MCA_FG_TYPE_PARALLEL:
		/* to do for parallel battery such as o81a */
		/* strateg_fg_ops_get_parallel_soh_new(fg, soh_new); */
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}
	return 0;
}

static unsigned long
strateg_fg_ops_get_parallel_calc_rvalue(struct strategy_fg *fg)
{
	unsigned long master_rvalue, slave_rvalue, rvalue;

	master_rvalue = platform_fg_ops_get_calc_rvalue(FG_IC_MASTER);
	slave_rvalue = platform_fg_ops_get_calc_rvalue(FG_IC_SLAVE);

	rvalue = (master_rvalue > slave_rvalue) ? master_rvalue : slave_rvalue;
	mca_log_info("rvalue=%ld, master_rvalue=%ld, slave_rvalue=%ld\n",
		     rvalue, master_rvalue, slave_rvalue);

	return rvalue;
}

static unsigned long strategy_fg_get_calc_rvalue(struct strategy_fg *fg)
{
	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		return platform_fg_ops_get_calc_rvalue(FG_IC_MASTER);
		break;
	case MCA_FG_TYPE_PARALLEL:
		return strateg_fg_ops_get_parallel_calc_rvalue(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}
	return 0;
}

static int strategy_fg_update_vcuttoff_voltage(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	int vcutoff_reg = 0;
	int ret = 0;

	ret = mca_battery_shutdown_update_vcutoff_para(fg);
	if (ret)
		return 0;

	vcutoff_reg = strategy_fg_get_cutoff_voltage(fg);

	if (vcutoff_reg != fg->vcutoff_fw) {
		strategy_fg_set_cutoff_voltage(fg, fg->vcutoff_fw);
		mca_log_err("vcutoff_reg:%d, vcutoff_fw %d", vcutoff_reg,
			    fg->vcutoff_fw);
	}
	return 0;
}

static int
strategy_fg_check_extreme_cold_temp_compensation(struct strategy_fg *fg,
						 int real_temp)
{
	int temp = real_temp;

	if (mca_smartchg_is_extreme_cold_enabled()) {
		for (int i = 0; i < fg->cfg.extreme_cold_para_size; i++) {
			if (fg->batt_cyclecount < fg->cfg.extreme_cold_para[i]
							  .cycle_count_max &&
			    real_temp < fg->cfg.extreme_cold_para[i].temp_max) {
				mca_log_err(
					"cc: %d, real_temp: %d, compensation: %d\n",
					fg->batt_cyclecount, real_temp,
					fg->cfg.extreme_cold_para[i]
						.compensation);
				temp = real_temp -
				       fg->cfg.extreme_cold_para[i].compensation;
				break;
			}
		}
	}

	return temp;
}

static int strategy_fg_get_parallel_temp(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_temp(FG_IC_SLAVE,
				       &(fg->slave_batt_info.temp));
	ret |= platform_fg_ops_get_temp(FG_IC_MASTER,
					&(fg->master_batt_info.temp));
	pr_err("%s master %d slave %d\n", __func__, fg->master_batt_info.temp,
	       fg->slave_batt_info.temp);
	if (ret)
		return -1;

	if (fg->master_batt_info.temp <= JEITA_COOL_THR_DEGREE ||
	    fg->slave_batt_info.temp <= JEITA_COOL_THR_DEGREE)
		fg->batt_info.temp = min(fg->master_batt_info.temp,
					 fg->slave_batt_info.temp);
	else if (min(fg->master_batt_info.temp, fg->slave_batt_info.temp) >
		 EXTREME_HIGH_DEGREE)
		fg->batt_info.temp = min(fg->master_batt_info.temp,
					 fg->slave_batt_info.temp);
	else
		fg->batt_info.temp = max(fg->master_batt_info.temp,
					 fg->slave_batt_info.temp);

	return 0;
}

static int strategy_fg_get_series_temp(struct strategy_fg *fg)
{
	return 0;
}

static int strategy_fg_get_parallel_original_temp(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_original_temp(FG_IC_SLAVE, &(fg->slave_batt_info.temp));
	ret |= platform_fg_ops_get_original_temp(FG_IC_MASTER, &(fg->master_batt_info.temp));
	pr_err("%s master %d slave %d\n", __func__, fg->master_batt_info.temp, fg->slave_batt_info.temp);
	if (ret)
		return -1;

	if (fg->master_batt_info.temp <= JEITA_COOL_THR_DEGREE ||
	    fg->slave_batt_info.temp <= JEITA_COOL_THR_DEGREE)
		fg->original_temp = min(fg->master_batt_info.temp, fg->slave_batt_info.temp);
	else if (min(fg->master_batt_info.temp, fg->slave_batt_info.temp) > EXTREME_HIGH_DEGREE)
		fg->original_temp = min(fg->master_batt_info.temp, fg->slave_batt_info.temp);
	else
		fg->original_temp = max(fg->master_batt_info.temp, fg->slave_batt_info.temp);

	return 0;
}

static int strategy_fg_update_original_temp(struct strategy_fg *fg)
{
	int ret = 0;

	if (fg->fg_error) {
		fg->original_temp = STRATEGY_FG_ERROR_FAKE_TEMP;
		return 0;
	}

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		ret = platform_fg_ops_get_original_temp(FG_IC_MASTER, &fg->original_temp);
		break;
	case MCA_FG_TYPE_PARALLEL:
		ret = strategy_fg_get_parallel_original_temp(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}

	return ret ? -1 : 0;
}

static int strategy_fg_update_batt_temp(struct strategy_fg *fg)
{
	int ret = 0;

	if (fg->fg_error) {
		fg->batt_temperature = STRATEGY_FG_ERROR_FAKE_TEMP;
		return 0;
	}

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		ret = platform_fg_ops_get_temp(FG_IC_MASTER,
					       &fg->batt_temperature);
		break;
	case MCA_FG_TYPE_PARALLEL:
		ret = strategy_fg_get_parallel_temp(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		ret = strategy_fg_get_series_temp(fg);
		break;
	default:
		return -1;
	}

	if (ret)
		return -1;

	fg->batt_temperature = strategy_fg_check_extreme_cold_temp_compensation(
		fg, fg->batt_temperature);
	return 0;
}

static int strategy_fg_get_parallel_error_state(struct strategy_fg *fg)
{
	bool fg_error_master;
	bool fg_error_slave;
	int ret;

	ret = platform_fg_ops_get_error_state(FG_IC_SLAVE, &fg_error_master);
	ret |= platform_fg_ops_get_error_state(FG_IC_MASTER, &fg_error_slave);
	pr_err("%s master %d slave %d\n", __func__, fg_error_master,
	       fg_error_slave);
	mca_log_info("master %d slave %d\n", fg_error_master, fg_error_slave);
	if (ret)
		return -1;

	if (fg_error_master | fg_error_slave)
		fg->fg_error = 1;
	else
		fg->fg_error = 0;

	fg->dual_error[FG_IC_MASTER] = fg_error_slave;
	fg->dual_error[FG_IC_SLAVE] = fg_error_master;

	return 0;
}

static int strategy_fg_get_error_state(struct strategy_fg *fg)
{
	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		platform_fg_ops_get_error_state(FG_IC_MASTER, &fg->fg_error);
		break;
	case MCA_FG_TYPE_PARALLEL:
		strategy_fg_get_parallel_error_state(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}
	return 0;
}

static int strategy_fg_update_fw(struct strategy_fg *fg)
{
	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		platform_fg_ops_update_fw(FG_IC_MASTER);
		break;
	case MCA_FG_TYPE_PARALLEL:
		platform_fg_ops_update_fw(FG_IC_MASTER);
		platform_fg_ops_update_fw(FG_IC_SLAVE);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}
	return 0;
}

static int strategy_fg_get_parallel_cyclecount(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_cyclecount(
		FG_IC_SLAVE, &(fg->slave_batt_info.cycle_count));
	ret |= platform_fg_ops_get_cyclecount(
		FG_IC_MASTER, &(fg->master_batt_info.cycle_count));
	if (ret)
		return -1;

	fg->batt_info.cycle_count = max(fg->master_batt_info.cycle_count,
					fg->slave_batt_info.cycle_count);

	return 0;
}

static int strategy_fg_get_series_cyclecount(struct strategy_fg *fg)
{
	return 0;
}

static int strategy_fg_update_cycle(struct strategy_fg *fg)
{
	int ret = 0;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		ret = platform_fg_ops_get_cyclecount(FG_IC_MASTER,
						     &fg->batt_cyclecount);
		break;
	case MCA_FG_TYPE_PARALLEL:
		return strategy_fg_get_parallel_cyclecount(fg);
	case MCA_FG_TYPE_SERIES:
		return strategy_fg_get_series_cyclecount(fg);
	default:
		return -1;
	}

	return ret;
}

static int strategy_fg_get_parallel_soh(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_soh(FG_IC_SLAVE, &(fg->slave_batt_info.soh));
	ret |= platform_fg_ops_get_soh(FG_IC_MASTER,
				       &(fg->master_batt_info.soh));
	if (ret)
		return -1;

	fg->batt_info.soh =
		max(fg->master_batt_info.soh, fg->slave_batt_info.soh);

	return 0;
}

static int strategy_fg_get_series_soh(struct strategy_fg *fg)
{
	return 0;
}

static int strategy_fg_update_soh(struct strategy_fg *fg)
{
	int ret = 0;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		ret = platform_fg_ops_get_soh(FG_IC_MASTER, &fg->batt_soh);
		break;
	case MCA_FG_TYPE_PARALLEL:
		return strategy_fg_get_parallel_soh(fg);
	case MCA_FG_TYPE_SERIES:
		return strategy_fg_get_series_soh(fg);
	default:
		return -1;
	}

	return 0;
}

static int strategy_fg_get_parallel_rm(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_rm(FG_IC_SLAVE, &(fg->slave_batt_info.rm));
	ret |= platform_fg_ops_get_rm(FG_IC_MASTER, &(fg->master_batt_info.rm));
	if (ret)
		return -1;

	fg->batt_info.rm = fg->master_batt_info.rm + fg->slave_batt_info.rm;

	return 0;
}

static int strategy_fg_get_series_rm(struct strategy_fg *fg)
{
	return 0;
}

static int strategy_fg_update_rm(struct strategy_fg *fg)
{
	int ret = 0;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		ret = platform_fg_ops_get_rm(FG_IC_MASTER, &fg->batt_rm);
		break;
	case MCA_FG_TYPE_PARALLEL:
		return strategy_fg_get_parallel_rm(fg);
	case MCA_FG_TYPE_SERIES:
		return strategy_fg_get_series_rm(fg);
	default:
		return -1;
	}

	return ret;
}

static int strategy_fg_get_parallel_fcc(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_fcc(FG_IC_SLAVE, &(fg->slave_batt_info.fcc));
	ret |= platform_fg_ops_get_fcc(FG_IC_MASTER,
				       &(fg->master_batt_info.fcc));
	if (ret)
		return -1;

	fg->batt_info.fcc = fg->master_batt_info.fcc + fg->slave_batt_info.fcc;

	return 0;
}

static int strategy_fg_get_series_fcc(struct strategy_fg *fg)
{
	return 0;
}

static int strategy_fg_update_fcc(struct strategy_fg *fg)
{
	int ret = 0;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		ret = platform_fg_ops_get_fcc(FG_IC_MASTER, &fg->batt_fcc);
		break;
	case MCA_FG_TYPE_PARALLEL:
		return strategy_fg_get_parallel_fcc(fg);
	case MCA_FG_TYPE_SERIES:
		return strategy_fg_get_series_fcc(fg);
	default:
		return -1;
	}

	return ret;
}

static int strategy_fg_get_parallel_volt(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_volt(FG_IC_MASTER,
				       &(fg->master_batt_info.volt));
	ret = platform_fg_ops_get_volt(FG_IC_SLAVE,
				       &(fg->slave_batt_info.volt));
	if (ret)
		return -1;

	fg->batt_info.volt =
		max(fg->master_batt_info.volt, fg->slave_batt_info.volt);

	return 0;
}

static int strategy_fg_get_series_volt(struct strategy_fg *fg)
{
	return 0;
}

static int strategy_fg_update_batt_volt(struct strategy_fg *fg)
{
	int ret = 0;

	if (!fg || !fg->fg_init_flag)
		return -1;

	if (fg->update_interval == STRATEGY_FG_WORK_INTERVAL_FAST)
		return 0;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		ret = platform_fg_ops_get_volt(FG_IC_MASTER, &fg->batt_voltage);
		break;
	case MCA_FG_TYPE_PARALLEL:
		return strategy_fg_get_parallel_volt(fg);
	case MCA_FG_TYPE_SERIES:
		return strategy_fg_get_series_volt(fg);
	default:
		return -1;
	}

	return ret;
}

static int strategy_fg_get_parallel_batt_info(struct strategy_fg *fg)
{
	int ret = 0;
	int i = 0;
	struct fg_batt_info batt_info;

	ret = strategy_fg_get_parallel_curr(fg, &(fg->batt_info.curr));
	ret |= strategy_fg_get_parallel_temp(fg);
	ret |= strategy_fg_get_parallel_rsoc(fg, &(fg->batt_info.rsoc));
	ret |= strategy_fg_get_parallel_volt(fg);
	ret |= strategy_fg_get_parallel_cyclecount(fg);
	ret |= strategy_fg_get_parallel_soh(fg);
	ret |= strategy_fg_get_parallel_rm(fg);
	ret |= strategy_fg_get_parallel_fcc(fg);

	for (i = 0; i <= 1; i++) {
		batt_info = i ? fg->slave_batt_info : fg->master_batt_info;
		mca_log_err(
			"fg_index[%d], CURRENT:%d RSOC:%d VOLTAGE:%d TEMP:%d\n",
			i, batt_info.curr, batt_info.rsoc, batt_info.volt,
			batt_info.temp);
		mca_log_info("fg_index[%d], CC:%d S:%d RM:%d F:%d\n", i,
			     batt_info.cycle_count, batt_info.soh, batt_info.rm,
			     batt_info.fcc);
	}

	return ret;
}

#define DFX_VBAT_DIFF_CHECK_DELAY_NS (180 * NSEC_PER_SEC)
#define DFX_VBAT_DIFF_THR 201

static void dfx_fg_check_vbat_diff(struct strategy_fg *fg)
{
	static bool checked;
	int diff;
	int param[2];

	if (fg->cfg.fg_type != MCA_FG_TYPE_PARALLEL)
		return;

	if (!checked) {
		s64 time_now = ktime_get_coarse_boottime_ns();

		if (time_now >= DFX_VBAT_DIFF_CHECK_DELAY_NS)
			checked = true;
		else
			mca_log_err("time_now: %lld\n",
				    time_now / NSEC_PER_SEC);
		return;
	}

	diff = abs(fg->master_batt_info.volt - fg->slave_batt_info.volt);
	mca_log_err("batt volt diff = %d\n", diff);
	if (diff >= DFX_VBAT_DIFF_THR) {
		param[0] = fg->master_batt_info.volt;
		param[1] = fg->slave_batt_info.volt;
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_VOLTAGE_DIFFER,
					  param, 2);
	} else {
		mca_charge_mievent_set_state(MIEVENT_STATE_END,
					     CHARGE_DFX_BATTERY_VOLTAGE_DIFFER);
	}
}

static int strategy_fg_get_batt_info(struct strategy_fg *fg)
{
	int ret = 0;

	if (!fg || !fg->fg_init_flag)
		return -1;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		ret = platform_fg_ops_get_batt_info(FG_IC_MASTER,
						    &fg->batt_info);
		break;
	case MCA_FG_TYPE_PARALLEL:
		ret = strategy_fg_get_parallel_batt_info(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}

	dfx_fg_check_vbat_diff(fg);

	return ret;
}

static int strategy_fg_set_clear_count_data(struct strategy_fg *fg)
{
	int ret = 0;

	if (!fg || !fg->fg_init_flag)
		return -1;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		platform_fg_ops_set_clear_count_data(FG_IC_MASTER);
		break;
	case MCA_FG_TYPE_PARALLEL:
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}

	return ret;
}

static void strategy_fg_report_battery_status_changed(struct strategy_fg *fg)
{
	static int last_soc, last_temp;
	int batt_temp = fg->batt_temperature / 10;

	if (fg->batt_ui_soc != last_soc || batt_temp != last_temp) {
		mca_log_err("ui_soc: %d => %d, temp: %d => %d\n", last_soc,
			    fg->batt_ui_soc, last_temp, batt_temp);
		last_soc = fg->batt_ui_soc;
		last_temp = batt_temp;
		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
				       MCA_EVENT_BATTERY_STS_CHANGE, NULL);
	}

	platform_class_buckchg_ops_get_pack_vbat(MAIN_BUCK_CHARGER,
						 &fg->pack_vbat);
	platform_class_buckchg_ops_get_pack_ibat(MAIN_BUCK_CHARGER,
						 &fg->pack_ibat);
	platform_class_buckchg_ops_get_pack_tbat(MAIN_BUCK_CHARGER,
						 &fg->pack_tbat);
}

enum mievent_socnotfull_ele {
	MIEVENT_SOCNOTFULL_PARAM_VOLTAGE,
	MIEVENT_SOCNOTFULL_PARAM_UISOC,
	MIEVENT_SOCNOTFULL_PARAM_RSOC,
	MIEVENT_SOCNOTFULL_PARAM_MAX,
};

static int mca_strategy_check_sigle_termination(struct strategy_fg *fg)
{
	static int count;
	int iterm = STRATEGY_FG_DEFAULT_ITERM;
	int vterm = STRATEGY_FG_DEFAULT_VTERM;
	int soft_iterm = STRATEGY_FG_DEFAULT_ITERM;
	int soft_vterm = STRATEGY_FG_DEFAULT_VTERM;
	int quick_charge_status = MCA_QUICK_CHG_STS_NO_CHARGING;
	int chgr_status = 0;
	int vbat_for_term = 0;
	int online = 0;
	int param[MIEVENT_SOCNOTFULL_PARAM_MAX];

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
				     STRATEGY_STATUS_TYPE_ONLINE, &online);
	if (online) {
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
					     STRATEGY_STATUS_TYPE_QC_CHARGE_STS,
					     &quick_charge_status);
	} else {
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					     STRATEGY_STATUS_TYPE_QC_CHARGE_STS,
					     &quick_charge_status);
	}

	if (fg->voter_ok) {
		iterm = mca_get_effective_result(fg->iterm_voter);
		vterm = mca_get_effective_result(fg->vterm_voter);
	}
	soft_iterm = iterm >= 1000 ? (iterm * 110 / 100) : (iterm * 120 / 100);
	soft_vterm = iterm >= 1000 ? (vterm - 5) : (vterm - 3);

	if (!fg->power_present ||
	    quick_charge_status == MCA_QUICK_CHG_STS_CHARGING ||
	    fg->charging_done || vterm <= fg->cfg.fg_hightemp_vterm)
		return 0;

	if (fg->cfg.fg_type == MCA_FG_TYPE_SINGLE_SERIES) {
		vbat_for_term = fg->batt_vcell_max * 2;
	} else {
		vbat_for_term = fg->batt_voltage;
	}

	mca_log_debug(
		"vterm: %d, iterm: %d, vbat: %d, vcell_max: %d, ibat: %d, pvbat: %d, pibat:%d, chgr_status: %d, charging_done: %d\n",
		vterm, iterm, fg->batt_voltage, fg->batt_vcell_max,
		fg->batt_current, fg->pack_vbat, fg->pack_ibat, chgr_status,
		fg->charging_done);

	platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER,
						  &chgr_status);
	if (chgr_status == MCA_BATT_CHGR_STATUS_TERMINATION) {
		strategy_class_fg_ops_set_charging_done(true);
		mca_log_err(
			"vterm: %d, iterm: %d, vbat: %d, vcell_max: %d, ibat: %d, pvbat: %d, chgr_status: %d, charging_done: %d\n",
			vterm, iterm, fg->batt_voltage, fg->batt_vcell_max,
			fg->batt_current, fg->pack_vbat, chgr_status,
			fg->charging_done);
	} else if (-fg->batt_current / 1000 < soft_iterm &&
		   vbat_for_term >= soft_vterm) {
		fg->near_vterm = true;
		if (++count >= 5) {
			count = 0;
			strategy_class_fg_ops_set_charging_done(true);
		}
		mca_log_err(
			"soft_vterm: %d, soft_iterm: %d, vbat: %d, vcell_max: %d, ibat: %d, pvbat: %d, count: %d, charging_done: %d\n",
			soft_vterm, soft_iterm, fg->batt_voltage,
			fg->batt_vcell_max, fg->batt_current, fg->pack_vbat, count,
			fg->charging_done);
	} else {
		fg->near_vterm = false;
		count = 0;
	}

	if (fg->charging_done && fg->batt_ui_soc != 100) {
		param[MIEVENT_SOCNOTFULL_PARAM_VOLTAGE] = fg->batt_voltage;
		param[MIEVENT_SOCNOTFULL_PARAM_UISOC] = fg->batt_ui_soc;
		param[MIEVENT_SOCNOTFULL_PARAM_RSOC] = fg->batt_rsoc;
		mca_charge_mievent_report(CHARGE_DFX_SOC_NOT_FULL, param,
					  MIEVENT_SOCNOTFULL_PARAM_MAX);
	}

	return 0;
}

static int strategy_fg_reset_co_to_default(struct strategy_fg *fg)
{
	int ret = 0;

	if (fg->fg1_batt_ctr_enabled == true) {
		ret = platform_fg_ops_set_co(FG_IC_MASTER, 0);
		fg->fg1_batt_ctr_enabled = false;
	} else if (fg->fg2_batt_ctr_enabled == true) {
		ret = platform_fg_ops_set_co(FG_IC_SLAVE, 0);
		fg->fg2_batt_ctr_enabled = false;
	}

	fg->monitor_soc_flag = false;
	fg->first_termination = false;
	fg->fg_lock_flag = false;

	return ret;
}

static void strategy_fg_reset_default_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	platform_fg_ops_set_co(FG_IC_MASTER, 0);
	platform_fg_ops_set_co(FG_IC_SLAVE, 0);
}

static int mca_strategy_parallel_first_termination(struct strategy_fg *fg)
{
	int ret = 0;
	int chgr_status = 0;
	int rsoc_master, rsoc_slave, tem_soc_delt;
	static int soc_delt;
	bool fc_master, fc_slave;

	ret = platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER,
							&chgr_status);
	if (ret) {
		mca_log_err("get chg status fail");
		return ret;
	}
	ret |= platform_fg_ops_get_rsoc(FG_IC_MASTER, &rsoc_master);
	ret |= platform_fg_ops_get_rsoc(FG_IC_SLAVE, &rsoc_slave);
	mca_log_err("fg_master_rsoc:%d, fg_slave_rsoc:%d", rsoc_master,
		    rsoc_slave);
	/*monitor battery to avoid locking*/
	if (fg->fg1_batt_ctr_enabled || fg->fg2_batt_ctr_enabled ||
	    fg->fg_lock_flag) {
		tem_soc_delt = abs(rsoc_master - rsoc_slave);
		if ((tem_soc_delt > 5) && !fg->fg_lock_flag) {
			if (!fg->monitor_soc_flag) {
				soc_delt = tem_soc_delt;
				fg->monitor_soc_flag = true;
			} else {
				if (soc_delt < tem_soc_delt - 1) {
					strategy_fg_reset_co_to_default(fg);
					fg->fg_lock_flag = true;
					fg->monitor_soc_flag = false;
				} else {
					fg->fg_lock_flag = false;
				}
			}
		} else if (fg->fg_lock_flag) {
			if (tem_soc_delt <= 4) {
				fg->fg_lock_flag = false;
			}
		} else {
			fg->monitor_soc_flag = false;
		}
	}

	if (chgr_status != MCA_BATT_CHGR_STATUS_FULLON &&
	    chgr_status != MCA_BATT_CHGR_STATUS_TAPER &&
	    chgr_status != MCA_BATT_CHGR_STATUS_PRECHARGE) {
		mca_log_err("not charging, no need take action");
		return ret;
	}

	ret |= platform_fg_ops_get_fc(FG_IC_MASTER, &fc_master);
	ret |= platform_fg_ops_get_fc(FG_IC_SLAVE, &fc_slave);
	mca_log_err("fc_master:%d, fc_slave:%d", fc_master, fc_slave);
	if (!fg->fg1_batt_ctr_enabled && !fg->fg2_batt_ctr_enabled) {
		if (fg->lossless_recharge) {
			if (fg->dual_force_full[FG_IC_MASTER]) {
				ret |= platform_fg_ops_set_co(FG_IC_MASTER,
							      true);
				fg->fg1_batt_ctr_enabled = true;
				fg->first_termination = false;
				mca_log_err(
					"FG_MASTER lossless_recharge_full is 1");
			} else if (fg->dual_force_full[FG_IC_SLAVE]) {
				ret |= platform_fg_ops_set_co(FG_IC_SLAVE,
							      true);
				fg->fg2_batt_ctr_enabled = true;
				fg->first_termination = false;
				mca_log_err(
					"FG_SLAVE lossless_recharge_full is 1");
			}
		} else {
			if (fc_master == true && !fg->fg_lock_flag) {
				ret |= platform_fg_ops_set_co(FG_IC_MASTER,
							      true);
				fg->fg1_batt_ctr_enabled = true;
				fg->first_termination = true;
				mca_log_err("FG_MASTER FC is 1");
			} else if (fc_slave == true && !fg->fg_lock_flag) {
				ret |= platform_fg_ops_set_co(FG_IC_SLAVE,
							      true);
				fg->fg2_batt_ctr_enabled = true;
				fg->first_termination = true;
				mca_log_err("FG_SLAVE FC is 1");
			}
		}
	}

	return ret;
}

static int strategy_fg_get_term_current_new(struct strategy_fg *fg,
					    int fg_index)
{
	static int last_temp_index[2] = { -1, -1 };
	struct term_curr_para *tc = &fg->cfg.term_curr_data[fg_index];
	struct term_curr_data *d = tc->iterm_data;
	int size = tc->iterm_para_size;
	int temp = (fg_index ? fg->slave_batt_info.temp :
			       fg->master_batt_info.temp) /
		   10;
	int last_idx = last_temp_index[fg_index];
	int idx, i, offset_flag = 0;

	/* largest index whose batt_temp is still at or below the read temp */
	for (i = size - 1; i >= 0; i--)
		if (d[i].batt_temp <= temp)
			break;
	idx = (i < 0) ? 0 : i;

	mca_log_info("fg[%d] temp_index %d, last_index %d", fg_index, idx,
		     last_idx);

	if (last_idx != -1 && idx < last_idx) {
		if (temp >= d[last_idx].batt_temp - d[last_idx].batt_temp_offset) {
			offset_flag = 1;
			idx = last_idx;
		}
	}
	mca_log_info("temp_offset_flag[%d]: %d", fg_index, offset_flag);
	last_temp_index[fg_index] = idx;
	fg->temp_offset_flag[fg_index] = offset_flag;

	return fg->fast_charge ? d[idx].ffc_iterm : d[idx].normal_iterm;
}

#define PARA_TERM_TEMP_WARM 300
#define PARA_TERM_TEMP_HOT 400
#define PARA_TERM_VTERM_SLAVE_HOT 20
#define PARA_TERM_VTERM_WARM 50
#define PARA_TERM_VTERM_HOT 65
#define PARA_TERM_VTERM_CO_COLD 20
#define PARA_TERM_VTERM_CO_WARM 30
#define PARA_TERM_VTERM_CO_HOT 60
#define QBG_CHG_DATA_DEFAULT_IBAT_MAX 500
#define QBG_CHG_DATA_DEFAULT_TERM_CURR 565
#define QBG_CHG_DATA_DEFAULT_TERM_VOLT 4480

static void strategy_fg_update_qbg_chg_data(struct strategy_fg *fg)
{
	int quick_charge_status = 0;

	if (!fg->cfg.support_qbg)
		return;

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					   STRATEGY_STATUS_TYPE_QC_CHARGE_STS,
					   &quick_charge_status);

	if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGING) {
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_QUICK_CHARGE,
			STRATEGY_STATUS_TYPE_QC_IBAT_MAX,
			&fg->qbg_chg_data.ibat_max);
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_QUICK_CHARGE,
			STRATEGY_STATUS_TYPE_QC_TERM_CURR,
			&fg->qbg_chg_data.term_curr);
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_QUICK_CHARGE,
			STRATEGY_STATUS_TYPE_QC_TERM_VOLT,
			&fg->qbg_chg_data.term_volt);
	} else if (!fg->voter_ok) {
		fg->qbg_chg_data.ibat_max = QBG_CHG_DATA_DEFAULT_IBAT_MAX;
		fg->qbg_chg_data.term_curr = QBG_CHG_DATA_DEFAULT_TERM_CURR;
		fg->qbg_chg_data.term_volt = QBG_CHG_DATA_DEFAULT_TERM_VOLT;
	} else {
		fg->qbg_chg_data.ibat_max =
			mca_get_effective_result(fg->charge_limit_voter);
		fg->qbg_chg_data.term_curr =
			mca_get_effective_result(fg->iterm_voter);
		fg->qbg_chg_data.term_volt =
			mca_get_effective_result(fg->vterm_voter);
	}

	fg->qbg_chg_data.chg_status = fg->chg_status;
	fg->qbg_chg_data.charging =
		quick_charge_status == MCA_QUICK_CHG_STS_CHARGING;
	platform_fg_ops_qbg_send_chg_data(FG_IC_MASTER);
}

static int strategy_fg_check_parallel_termination(struct strategy_fg *fg)
{
	static bool last_first_termination;
	int ret, vterm_target = 0;
	int vterm = 0;
	int final_term_curr = 0;
	static int last_term[2] = { 177, 58 };
	bool fc_master;

	//the first termination
	ret = mca_strategy_parallel_first_termination(fg);

	//adjust vterm after first_termination
	if (fg->cfg.support_base_flip &&
	    last_first_termination != fg->first_termination) {
		last_first_termination = fg->first_termination;
		if (last_first_termination == true) {
			int offset = 0;
			bool bump = true;

			vterm = mca_get_effective_result(fg->vterm_voter);
			if (fg->fg1_batt_ctr_enabled) {
				if (!fg->co_ctrl_active &&
				    fg->slave_batt_info.temp >=
					    PARA_TERM_TEMP_HOT)
					offset = PARA_TERM_VTERM_SLAVE_HOT;
			} else if (fg->fg2_batt_ctr_enabled) {
				int temp = fg->master_batt_info.temp;

				if (!fg->co_ctrl_active)
					offset = temp < PARA_TERM_TEMP_HOT ?
							 PARA_TERM_VTERM_WARM :
							 PARA_TERM_VTERM_HOT;
				else if (temp < PARA_TERM_TEMP_WARM)
					offset = PARA_TERM_VTERM_CO_COLD;
				else
					offset = temp < PARA_TERM_TEMP_HOT ?
							 PARA_TERM_VTERM_CO_WARM :
							 PARA_TERM_VTERM_CO_HOT;
			} else {
				bump = false;
			}

			if (bump)
				vterm_target = (fg->fast_charge ? offset : 0) +
					       vterm;
			mca_log_err("last_first_termination:vterm_target:%d",
				    vterm_target);
			if (vterm_target == vterm)
				goto term_curr;
			mca_vote_override(fg->vterm_voter, "para_term", true,
					  vterm_target);
		} else {
			mca_log_err("last_first_termination: clean para_term");
			mca_vote_override(fg->vterm_voter, "para_term", false,
					  0);
		}
	}

term_curr:
	//setting dual batetry term current
	for (int i = 0; i < 2; i++) {
		fg->dual_iterm[i] = strategy_fg_get_term_current_new(fg, i);
		if (last_term[i] != fg->dual_iterm[i])
			last_term[i] = fg->dual_iterm[i];
		mca_log_err("fg_index: %d, last_term: %d, dual_iterm :%d\n", i,
			    last_term[i], fg->dual_iterm[i]);
	}

	if (!fg->fg2_batt_ctr_enabled && fg->fg1_batt_ctr_enabled) {
		final_term_curr = last_term[FG_IC_SLAVE];
	} else if (fg->fg2_batt_ctr_enabled && !fg->fg1_batt_ctr_enabled) {
		final_term_curr = last_term[FG_IC_MASTER];
	} else if (!fg->fg2_batt_ctr_enabled && !fg->fg1_batt_ctr_enabled) {
		final_term_curr = last_term[FG_IC_SLAVE];
		if (final_term_curr == 0)
			final_term_curr = last_term[FG_IC_MASTER];
	}

	if (fg->cfg.support_base_flip) {
		ret |= platform_fg_ops_get_fc(FG_IC_MASTER, &fc_master);
		if (fg->fast_charge && fg->batt_temperature >= 401 &&
		    fg->fg2_batt_ctr_enabled && !fc_master)
			mca_vote(fg->iterm_voter, "high_temp_para", true, 200);
		else
			mca_vote(fg->iterm_voter, "high_temp_para", false, 0);
	}

	mca_log_info("final_term_curr:%d", final_term_curr);
	if (final_term_curr != fg->last_final_term_curr) {
		fg->last_final_term_curr = final_term_curr;
		mca_vote(fg->iterm_voter, "para_term", true, final_term_curr);
	}

	mca_strategy_check_sigle_termination(fg);
	return 0;
}

static int mca_strategy_check_termination(struct strategy_fg *fg)
{
	int ret = 0;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		ret = mca_strategy_check_sigle_termination(fg);
		break;
	case MCA_FG_TYPE_PARALLEL:
		ret = strategy_fg_check_parallel_termination(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}

	return ret;
}

static void mca_strategy_single_force_fw_report_full(struct strategy_fg *fg)
{
	int force_iterm, force_vterm;
	int iterm = STRATEGY_FG_DEFAULT_ITERM;
	int vterm = STRATEGY_FG_DEFAULT_VTERM;
	static int force_full_count;
	int quick_charge_jeita_iterm = 0;
	int quick_charge_status = MCA_QUICK_CHG_STS_NO_CHARGING;
	int online;

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
				     STRATEGY_STATUS_TYPE_ONLINE, &online);
	if (online) {
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
					     STRATEGY_STATUS_TYPE_QC_CHARGE_STS,
					     &quick_charge_status);
	} else {
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					     STRATEGY_STATUS_TYPE_QC_CHARGE_STS,
					     &quick_charge_status);
	}

	if (fg->voter_ok) {
		iterm = mca_get_effective_result(fg->iterm_voter);
		vterm = mca_get_effective_result(fg->vterm_voter);
	}

	if (!fg->cfg.terminated_by_cp) {
		if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGING)
			return;
		force_iterm = iterm * 119 / 100;
		force_vterm = vterm - 15;
	} else {
		force_vterm = vterm - 15;
		if (quick_charge_status != MCA_QUICK_CHG_STS_CHARGING ||
		    fg->batt_voltage < force_vterm)
			return;
		if (online) {
			mca_strategy_func_get_status(
				STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
				STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM,
				&quick_charge_jeita_iterm);
		} else {
			mca_strategy_func_get_status(
				STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM,
				&quick_charge_jeita_iterm);
		}
		force_iterm = quick_charge_jeita_iterm * 130 / 100;
	}

	mca_log_info(
		"terminated_by_cp = %d, force_iterm = %d, force_vterm = %d, start_force_full = %d",
		fg->cfg.terminated_by_cp, force_iterm, force_vterm,
		fg->start_force_full);

	if (fg->power_present && fg->fast_charge && !fg->charging_done) {
		if (-fg->batt_current / 1000 < force_iterm &&
		    fg->batt_voltage > force_vterm && !fg->start_force_full) {
			force_full_count++;
			mca_log_info("force_full_count = %d", force_full_count);
			if (force_full_count >= 6) {
				platform_fg_ops_set_force_report_full(
					FG_IC_MASTER);
				force_full_count = 0;
				fg->start_force_full = true;
				mca_log_info("force fw report full");
				schedule_delayed_work(
					&fg->force_report_full_work,
					msecs_to_jiffies(8000));
			}
		} else {
			force_full_count = 0;
		}
	} else {
		force_full_count = 0;
		fg->start_force_full = false;
	}
}

static void mca_strategy_parallel_force_fw_report_full(struct strategy_fg *fg,
						       int fg_index)
{
	int force_iterm, force_vterm;
	int count = 6;
	int iterm = STRATEGY_FG_DEFAULT_ITERM;
	int vterm = STRATEGY_FG_DEFAULT_VTERM;
	struct fg_batt_info batt_info;
	static int force_full_count[2] = { 0, 0 };

	if (!fg->co_ctrl_support)
		return;
	if (mca_get_effective_result(fg->en_voter) == 0)
		return;

	batt_info = fg_index ? fg->slave_batt_info : fg->master_batt_info;

	if (fg->cfg.base_flip_same)
		count = fg->cold_zone ? 3 :
			(fg->lossless_recharge ? 2 : 6);
	else if (fg->lossless_recharge && fg->cfg.support_base_flip)
		count = 2;
	else
		count = 6;

	if (fg->voter_ok) {
		iterm = fg->dual_iterm[fg_index];
		vterm = mca_get_client_vote(fg->vterm_voter, "jeita");
	} else {
		iterm = STRATEGY_FG_DEFAULT_ITERM;
		vterm = STRATEGY_FG_DEFAULT_VTERM;
	}

	if (fg->fg1_batt_ctr_enabled || fg->fg2_batt_ctr_enabled) {
		force_vterm = vterm - 30;
		force_iterm = iterm * 130 / 100;
	} else if (fg->cfg.base_flip_same) {
		force_vterm = vterm - 15;
		force_iterm = fg->fast_charge ? iterm * 115 / 100 : iterm + 30;
	} else if (fg->fast_charge) {
		force_vterm = vterm - 30;
		force_iterm = iterm * 230 / 100;
	} else {
		force_vterm = vterm - 25;
		if (fg->co_ctrl_active) {
			if (fg->batt_rsoc < 98)
				force_iterm = iterm + (fg_index == 0 ? 33 : 12);
			else
				force_iterm = iterm + 33;
		} else {
			if (fg->batt_temperature > 30)
				force_iterm = iterm + (fg_index == 0 ? 33 : 12);
			else
				force_iterm = iterm + (fg_index == 0 ? 13 : 12);
		}
	}

	mca_log_info(
		"fg_index[%d], vterm: %d, iterm: %d, force_iterm: %d, force_vterm: %d, start_force_full: %d\n",
		fg_index, vterm, iterm, force_iterm, force_vterm,
		fg->dual_force_full[fg_index]);

	if (fg->co_ctrl_support && batt_info.temp <= 479 &&
	    !fg->charging_done) {
		if (-batt_info.curr / 1000 < force_iterm &&
		    batt_info.volt > force_vterm &&
		    !fg->dual_force_full[fg_index]) {
			force_full_count[fg_index]++;
			mca_log_err("force_full_count[%d] =  %d", fg_index,
				    force_full_count[fg_index]);
			if (force_full_count[fg_index] >= count) {
				if (!fg->co_ctrl_active &&
				    fg->cfg.support_base_flip &&
				    !fg->fg1_batt_ctr_enabled &&
				    fg_index == FG_IC_MASTER &&
				    !fg->fg2_batt_ctr_enabled) {
					int status = 0;

					mca_strategy_func_get_status(
						STRATEGY_FUNC_TYPE_QUICK_CHARGE,
						STRATEGY_STATUS_TYPE_QC_CHARGE_STS,
						&status);
					if (status == MCA_QUICK_CHG_STS_CHARGING) {
						mca_strategy_func_process(
							STRATEGY_FUNC_TYPE_QUICK_CHARGE,
							MCA_EVENT_MASTER_BATT_CLOSE,
							fg->dual_iterm[FG_IC_SLAVE]);
						force_full_count[FG_IC_MASTER] = 0;
						fg->switch_pmic_term = true;
						mca_log_err(
							"switch to pmic termination\n");
						return;
					}
				}
				platform_fg_ops_set_force_report_full(fg_index);
				force_full_count[fg_index] = 0;
				fg->dual_force_full[fg_index] = true;
				mca_log_err("start report full");
				//dual G2 self equal
				if (fg->self_equal_flag[fg_index]) {
					fg->self_equal_count[fg_index] = 1;
					if (fg->self_equal_max_count <= 1) {
						fg->self_equal_count[fg_index] =
							0;
						fg->self_equal_flag[fg_index] =
							false;
					}
					mca_log_err(
						"start equal report full, self_equal_count[%d]: %d",
						fg_index,
						fg->self_equal_count[fg_index]);
				} else {
					fg->self_equal_count[fg_index] = 0;
				}
			}
		} else {
			force_full_count[fg_index] = 0;
			fg->dual_force_full[fg_index] = false;
		}
	} else {
		force_full_count[fg_index] = 0;
		fg->dual_force_full[fg_index] = false;
	}
}

static void
strategy_fg_check_parallel_force_fw_report_full(struct strategy_fg *fg)
{
	int soh_diff, cycle_diff;

	if ((fg->master_batt_info.cycle_count / 50) ||
	    (fg->slave_batt_info.cycle_count / 50)) {
		soh_diff = fg->master_batt_info.soh - fg->slave_batt_info.soh;
		cycle_diff = fg->master_batt_info.cycle_count -
			     fg->slave_batt_info.cycle_count;

		if (abs(soh_diff) >= 5) {
			if (soh_diff)
				fg->self_equal_flag[0] = true;
			else
				fg->self_equal_flag[1] = true;
		} else if (abs(cycle_diff) >= 10) {
			if (cycle_diff)
				fg->self_equal_flag[0] = true;
			else
				fg->self_equal_flag[1] = true;
		}
		mca_log_info("soh_diff: %d, cycle_diff: %d", soh_diff,
			     cycle_diff);
	}

	mca_strategy_parallel_force_fw_report_full(fg, FG_IC_MASTER);
	mca_strategy_parallel_force_fw_report_full(fg, FG_IC_SLAVE);
}

static int mca_strategy_force_fw_report_full(struct strategy_fg *fg)
{
	int ret = 0;

	if (!fg || !fg->fg_init_flag)
		return -1;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		mca_strategy_single_force_fw_report_full(fg);
		break;
	case MCA_FG_TYPE_PARALLEL:
		strategy_fg_check_parallel_force_fw_report_full(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}

	return ret;
}

static void mca_strategy_start_recharging(struct strategy_fg *fg)
{
	mca_log_err("start to recharging\n");
	fg->recharging = true;
	strategy_class_fg_ops_set_charging_done(false);
	strategy_class_fg_set_fastcharge(false);
	mca_vote(fg->en_voter, "term_recharge", false, 0);

	mca_mod_delayed_work(&fg->monitor_work,
			     msecs_to_jiffies(STRATEGY_FG_WORK_INTERVAL_FAST));
}

#define EU_RECHARGER_SOC 95
static int charge_get_batt_rm_threshold(void)
{
	static int device_power_max;
	int batt_rm_thr;

	if (device_power_max == 0)
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_QUICK_CHARGE,
			STRATEGY_STATUS_TYPE_QC_MAX_POWER, &device_power_max);

	if (device_power_max > CHARGE_SLOWLY_POWER_HIGH_MW)
		batt_rm_thr = CHARGE_SLOWLY_RM_THR_HIGH;
	else if (device_power_max >= CHARGE_SLOWLY_POWER_MID_MW)
		batt_rm_thr = CHARGE_SLOWLY_RM_THR_MID;
	else
		batt_rm_thr = CHARGE_SLOWLY_RM_THR_LOW;

	mca_log_info("device_power_max:%d, batt_rm_thr:%d\n", device_power_max,
		     batt_rm_thr);
	return batt_rm_thr;
}

static void charge_slowly_monitor_func(struct strategy_fg *fg)
{
	static time64_t nowtime, nowplugintime;
	static int now_rm, now_soc;
	unsigned int powermax = 0;
	int batt_rm_thr, usable_capacity, expect_soc;
	int dfx_data[CHARGE_SLOWLY_DFX_PARA_NUM];

	mca_log_err("realtype:%d, soc:%d, rm:%d\n", fg->real_type,
		    fg->batt_ui_soc, now_rm);
	mca_log_err("time:%d, timeplugin:%d plugin:%d\n", (int)nowtime,
		    (int)nowplugintime, (int)fg->online_time);
	nowplugintime = ktime_get_boottime_seconds();

	if (fg->real_type != XM_CHARGER_TYPE_PPS) {
		if (fg->real_type >= XM_CHARGER_TYPE_SDP &&
		    fg->real_type <= XM_CHARGER_TYPE_PD_VERIFY &&
		    !fg->non_std_charger_reported &&
		    nowplugintime - fg->online_time >
			    NON_STD_CHARGER_PLUGIN_TIME_S) {
			dfx_data[0] = fg->real_type;
			mca_charge_mievent_report(CHARGE_DFX_NON_STANDARD_CHARGER,
						  dfx_data, 1);
			fg->non_std_charger_reported = true;
			mca_log_err("report dfx\n");
		}
		goto reset;
	}

	if (fg->batt_ui_soc > CHARGE_SLOWLY_SOC_MAX)
		goto reset;

	fg->online_time = ktime_get_boottime_seconds();
	(void)protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS,
						   &powermax);
	batt_rm_thr = charge_get_batt_rm_threshold();
	mca_log_err("powermax:%d designcapacity:%d curr_batt_rm_thr:%d\n",
		    powermax, fg->batt_dc, batt_rm_thr);

	if ((int)powermax <= CHARGE_SLOWLY_POWER_MIN_W ||
	    batt_rm_thr * 1000 > fg->batt_dc)
		return;

	if (fg->slow_chg_start_time == 0) {
		fg->slow_chg_initial_rm = fg->batt_rm / 1000;
		fg->slow_chg_initial_soc = fg->batt_ui_soc;
		fg->slow_chg_start_time = ktime_get_boottime_seconds();
	}

	nowtime = ktime_get_boottime_seconds();
	now_rm = fg->batt_rm / 1000;
	now_soc = fg->batt_ui_soc;
	if (nowtime - fg->slow_chg_start_time <= CHARGE_SLOWLY_CHECK_PERIOD_S)
		return;

	usable_capacity = fg->batt_dc / 1000 - CHARGE_SLOWLY_CAPACITY_MARGIN;
	expect_soc = usable_capacity ? (batt_rm_thr * 100) / usable_capacity : 0;
	if (now_soc - fg->slow_chg_initial_soc < expect_soc) {
		mca_log_err("ntime:%d, initialtime:%d\n", (int)nowtime,
			    (int)fg->slow_chg_start_time);
		mca_log_err("nrm:%d, initialrm:%d\n", now_rm,
			    fg->slow_chg_initial_rm);
		mca_log_err("report dfx\n");
		dfx_data[0] = powermax;
		dfx_data[1] = fg->slow_chg_initial_rm;
		dfx_data[2] = now_rm;
		dfx_data[3] = fg->slow_chg_initial_soc;
		dfx_data[4] = now_soc;
		mca_charge_mievent_report(CHARGE_DFX_CHARGE_SLOWLY, dfx_data,
					  CHARGE_SLOWLY_DFX_PARA_NUM);
	}
	fg->slow_chg_initial_rm = now_rm;
	fg->slow_chg_initial_soc = now_soc;
	fg->slow_chg_start_time = nowtime;
	return;

reset:
	fg->slow_chg_start_time = 0;
	nowtime = 0;
	now_rm = 0;
	now_soc = 0;
}

static int mca_strategy_check_recharge(struct strategy_fg *fg)
{
	if (!fg || !fg->power_present || !fg->charging_done || fg->recharging)
		return 0;

	if (!fg->is_eu_model) {
		if (fg->batt_raw_soc <= fg->cfg.report_full_raw_soc) {
			mca_strategy_start_recharging(fg);
			mca_log_err("ok to recharging, raw_soc: %d\n",
				    fg->batt_raw_soc);
		}
	} else {
		if (fg->batt_ui_soc < EU_RECHARGER_SOC) {
			mca_strategy_start_recharging(fg);
			mca_log_err("eu start recharging ui_soc: %d rsoc:%d\n",
				    fg->batt_ui_soc, fg->batt_rsoc);
		}
	}

	return 0;
}

static int mca_strategy_get_ffc_safe_item(struct strategy_fg *fg)
{
	int temp_index = 0, cycle_index = 0;

	if (fg->batt_cyclecount <= 100)
		cycle_index = 0;
	else if (fg->batt_cyclecount <= 300)
		cycle_index = 1;
	else if (fg->batt_cyclecount <= 500)
		cycle_index = 2;
	else if (fg->batt_cyclecount <= 800)
		cycle_index = 3;
	else
		cycle_index = 4;

	if (fg->batt_temperature < 200)
		temp_index = 0;
	else if (fg->batt_temperature < 250)
		temp_index = 1;
	else if (fg->batt_temperature < 300)
		temp_index = 2;
	else if (fg->batt_temperature < 350)
		temp_index = 3;
	else if (fg->batt_temperature < 400)
		temp_index = 4;
	else if (fg->batt_temperature < 450)
		temp_index = 5;
	else
		temp_index = 6;

	return fg->cfg.ffc_safe_item[cycle_index][temp_index];
}

static int mca_strategy_get_fl4p0_iterm(struct strategy_fg *fg)
{
	int iterm;
	int safe_item = 0;
	int calibration_ffc_iterm = 0;

	safe_item = mca_strategy_get_ffc_safe_item(fg);
	platform_fg_ops_get_calibration_ffc_iterm(FG_IC_MASTER,
						  &calibration_ffc_iterm);
	iterm = max(safe_item, calibration_ffc_iterm);

	mca_log_info("safe_item: %d, calibration_ffc_iterm: %d, iterm: %d\n",
		     safe_item, calibration_ffc_iterm, iterm);

	return iterm;
}

static void mca_strategy_check_fl4p0_status(struct strategy_fg *fg)
{
	static int count;
	int real_supplement_energy;
	int iterm = STRATEGY_FG_DEFAULT_ITERM;
	int vterm = STRATEGY_FG_DEFAULT_VTERM;

	if (!fg || fg->charging_done || fg->recharging || !fg->voter_ok ||
	    !fg->fast_charge || fg->ffc_continue_charge)
		return;

	if (fg->fast_charge)
		mca_vote(fg->iterm_voter, "calibration", true,
			 mca_strategy_get_ffc_safe_item(fg));
	else
		mca_vote(fg->iterm_voter, "calibration", false, 0);

	iterm = mca_strategy_get_fl4p0_iterm(fg);
	vterm = mca_get_effective_result(fg->vterm_voter);

	if (-fg->batt_current / 1000 < iterm &&
	    (fg->batt_voltage >= vterm - 11) &&
	    vterm >= STRATEGY_FG_DEFAULT_VTERM && !fg->ffc_continue_charge) {
		if (count++ >= 2) {
			count = 0;
			fg->ffc_continue_charge = true;
		}
		mca_log_info("vterm: %d, iterm: %d, vbat: %d, ibat: %d", vterm,
			     iterm, fg->batt_voltage, fg->batt_current);
	} else {
		count = 0;
	}

	if (fg->ffc_continue_charge) {
		fg->calibration_temp = fg->batt_temperature;
		platform_fg_ops_get_real_supplement_energy(
			FG_IC_MASTER, &real_supplement_energy);
		if (real_supplement_energy == 0xFFFF) {
			mca_log_info("ok to calibration iterm");
			strategy_class_fg_ops_set_charging_done(true);
		} else {
			mca_log_info("continue calibration");
			schedule_delayed_work(&fg->fl4p0_calibration_work, 0);
		}
	}
}

#define EU_LOSSES_START_CYCLECOUNT 300
static int mca_strategy_check_lossless_recharge(struct strategy_fg *fg)
{
	static ktime_t last_time;
	ktime_t time_now = ktime_get();
	ktime_t time_diff = 0;
	int batt_temp = 0;
	int vterm = 0;

	if (!fg->charging_done) {
		last_time = time_now;
		return 0;
	}

	if (fg->is_eu_model) {
		if (fg->batt_cyclecount <= EU_LOSSES_START_CYCLECOUNT) {
			fg->lossless_recharge = false;
			mca_log_info("eu model CycleCount Too Low Disable\n");
			return 0;
		}
	}

	time_diff = ktime_to_ms(ktime_sub(time_now, last_time));
	time_diff /= 1000;
	mca_log_info("time_now: %lld, last_time: %lld, time_diff: %lld\n",
		     time_now, last_time, time_diff);

	(void)strategy_class_fg_ops_get_temperature(&batt_temp);
	batt_temp /= 10;

	if (fg->charging_done && fg->fast_charge &&
	    batt_temp >= fg->cfg.rechg_cfg.temp_low &&
	    batt_temp <= fg->cfg.rechg_cfg.temp_high &&
	    time_diff >= fg->cfg.rechg_cfg.rest_time) {
		fg->lossless_recharge = true;
		if (fg->cfg.support_base_flip) {
			vterm = mca_get_effective_result(fg->vterm_voter);
			mca_vote(fg->vterm_voter, "losslessRec", true,
				 vterm - 10);
			mca_vote(fg->charge_limit_voter, "losslessRec", true,
				 350);
		}
	}
	mca_log_info(
		"charging_done: %d, fast_charge: %d, temp: %d, lossless_recharge: %d\n",
		fg->charging_done, fg->fast_charge, batt_temp,
		fg->lossless_recharge);

	if (fg->lossless_recharge) {
		mca_strategy_start_recharging(fg);
		mca_log_err("ok to lossless_recharge\n");
	}

	return 0;
}

static int mca_strategy_update_fg_info(struct strategy_fg *fg)
{
	static int exit_lowpower_pending;
	int ret;

	strategy_fg_get_error_state(fg);
	if (fg->fg_error) {
		if (fg->cfg.support_base_flip &&
		    fg->dual_error[FG_IC_MASTER] &&
		    !fg->dual_error[FG_IC_SLAVE])
			exit_lowpower_pending = 1;

		mca_charge_mievent_report(CHARGE_DFX_FG_IIC_ERR,
					  &fg->batt_ui_soc, 1);
		fg->batt_ui_soc = STRATEGY_FG_ERROR_FAKE_SOC;
		fg->batt_temperature = STRATEGY_FG_ERROR_FAKE_TEMP;
		fg->original_temp = STRATEGY_FG_ERROR_FAKE_TEMP;
		strategy_fg_report_battery_status_changed(fg);
		return -1;
	}

	if (exit_lowpower_pending == 1 && fg->power_present) {
		mca_log_err(
			"last master i2c error and slave i2c is ok, now dual i2c is ok, so set exit lowpower \n");
		platform_class_loadsw_set_lowpower_mode(LOADSW_ROLE_MASTER,
							false);
		exit_lowpower_pending = 0;
	}

	ret = strategy_fg_get_batt_info(fg);
	if (ret)
		return ret;

	if (fg->cyclecount_hundred != fg->batt_cyclecount / 100 &&
	    fg->batt_cyclecount % 100 == 0) {
		fg->cyclecount_hundred = fg->batt_cyclecount / 100;
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_CYCLECOUNT,
					  &fg->batt_cyclecount, 1);
		strategy_fg_set_clear_count_data(fg);
	}

	fg->batt_current = fg->batt_info.curr;
	fg->batt_voltage = fg->batt_info.volt;
	fg->batt_vcell_max = fg->batt_info.vcell_max;
	fg->batt_temperature = strategy_fg_check_extreme_cold_temp_compensation(
		fg, fg->batt_info.temp);
	strategy_fg_update_original_temp(fg);
	fg->batt_rsoc = fg->batt_info.rsoc;
	fg->batt_rm = fg->batt_info.rm;
	fg->batt_fcc = fg->batt_info.fcc;
	fg->batt_cyclecount = fg->batt_info.cycle_count;
	fg->batt_soh = fg->batt_info.soh;
	fg->batt_raw_soc =
		((fg->batt_rm / 1000) * 10000) / (fg->batt_fcc / 1000);

	if (0) {
		ret = strategy_fg_ops_get_curr(fg, &fg->batt_current);
		ret |= strategy_fg_update_batt_temp(fg);
		ret |= strategy_fg_ops_get_rsoc(fg, &fg->batt_rsoc);
		ret |= strategy_fg_update_batt_volt(fg);
		ret |= strategy_fg_update_cycle(fg);
		ret |= strategy_fg_update_soh(fg);
		ret |= strategy_fg_update_rm(fg);
		ret |= strategy_fg_update_fcc(fg);
	}
	ret |= strategy_fg_get_dod_count(fg, &fg->dod_count);
	fg->fast_charge = strategy_fg_get_fast_charge(fg);

	return ret;
}

#define BATTERY_TEMP_HOT_THRESHOLD 550
#define BATTERY_TEMP_COLD_THRESHOLD -100

enum mievent_battery_temp_abnormal_ele {
	MIEVENT_BATTERY_TEMP_PARAM_TBAT,
	MIEVENT_BATTERY_TEMP_PARAM_TBAT_MAX,
	MIEVENT_BATTERY_TEMP_PARAM_TBAT_MIN =
		MIEVENT_BATTERY_TEMP_PARAM_TBAT_MAX,
	MIEVENT_BATTERY_TEMP_PARAM_IS_CHARGING,
	MIEVENT_BATTERY_TEMP_PARAM_TBOARD,
	MIEVENT_BATTERY_TEMP_PARAM_MAX,
};

static int fg_abnormal_temp_notify_mievent(struct strategy_fg *fg)
{
	int param[MIEVENT_BATTERY_TEMP_PARAM_MAX] = { 0 };

	if (fg->batt_temperature <= BATTERY_TEMP_HOT_THRESHOLD &&
	    fg->batt_temperature >= BATTERY_TEMP_COLD_THRESHOLD) {
		mca_log_info("battery temp normal ignore");
		return 0;
	}

	param[MIEVENT_BATTERY_TEMP_PARAM_TBAT] = fg->batt_temperature;
	param[MIEVENT_BATTERY_TEMP_PARAM_IS_CHARGING] =
		(fg->chg_status == POWER_SUPPLY_STATUS_CHARGING) ? 1 : 0;
	param[MIEVENT_BATTERY_TEMP_PARAM_TBOARD] = fg->thermal_board_temp;
	if (fg->batt_temperature < BATTERY_TEMP_COLD_THRESHOLD) {
		platform_fg_ops_get_temp_min(
			FG_IC_MASTER,
			&param[MIEVENT_BATTERY_TEMP_PARAM_TBAT_MIN]);
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_TEMP_COLD, param,
					  MIEVENT_BATTERY_TEMP_PARAM_MAX);
	} else if (fg->batt_temperature > BATTERY_TEMP_HOT_THRESHOLD) {
		platform_fg_ops_get_temp_max(
			FG_IC_MASTER,
			&param[MIEVENT_BATTERY_TEMP_PARAM_TBAT_MAX]);
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_TEMP_HOT, param,
					  MIEVENT_BATTERY_TEMP_PARAM_MAX);
	}

	return 0;
}

#define DFS_CHECK_BATT_AUTH_TIME_SEC 300
static int fg_check_batt_auth(struct strategy_fg *fg)
{
	time64_t time_now;
	static bool checked = false;

	if (!checked) {
		time_now = ktime_get_boottime_seconds();
		if (time_now > DFS_CHECK_BATT_AUTH_TIME_SEC) {
			mca_log_info("check batt_auth: %d, time_now: %lld\n",
				     fg->batt_auth, time_now);
			checked = true;
			if (!fg->batt_auth)
				mca_charge_mievent_report(
					CHARGE_DFX_BATTERY_AUTH_FAIL, NULL, 0);
		}
	}

	return 0;
}

#define BATT_OVERHEAT_THRESHOLD 580
#define BATT_WARM_THRESHOLD 480
#define BATT_COOL_THRESHOLD 150
#define BATT_COLD_THRESHOLD 0

static int fg_update_batt_health(struct strategy_fg *fg)
{
	int temp = fg->batt_temperature;
	int health = POWER_SUPPLY_HEALTH_GOOD;

	if (temp >= BATT_OVERHEAT_THRESHOLD)
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (temp >= BATT_WARM_THRESHOLD && temp < BATT_OVERHEAT_THRESHOLD)
		health = POWER_SUPPLY_HEALTH_WARM;
	else if (temp >= BATT_COOL_THRESHOLD && temp < BATT_WARM_THRESHOLD)
		health = POWER_SUPPLY_HEALTH_GOOD;
	else if (temp >= BATT_COLD_THRESHOLD && temp < BATT_COOL_THRESHOLD)
		health = POWER_SUPPLY_HEALTH_COOL;
	else if (temp < BATT_COLD_THRESHOLD)
		health = POWER_SUPPLY_HEALTH_COLD;

	if (health != fg->batt_health) {
		fg->batt_health = health;
		mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
				       MCA_EVENT_BATTERY_HEALTH_CHANGE, NULL);
	}
	return 0;
}

#define BATTEY_CAPACITY_CRITICAL_LOW 10
#define BATTERY_CYCLE_COUNT_PERIOID 100
static int fg_update_status(struct strategy_fg *fg, bool prohibit_jump)
{
	int ret;
	union power_supply_propval pval = { 0 };

	if (fg->batt_psy) {
		power_supply_get_property(fg->batt_psy,
					  POWER_SUPPLY_PROP_STATUS, &pval);
		fg->chg_status = pval.intval;
	} else {
		fg->batt_psy = power_supply_get_by_name("battery");
	}

	ret = mca_strategy_update_fg_info(fg);
	if (ret || fg->fg_error) {
		mca_log_err("get batt info fail\n");
		return -1;
	}
	fg_update_batt_health(fg);
	strategy_fg_record_volt_mean(fg);
	strategy_fg_record_curr_mean(fg);
	strategy_fg_update_vcuttoff_voltage(fg);
	strategy_fg_update_fw(fg);

	fg->batt_ui_soc = fg_ui_soc_smooth(fg, prohibit_jump);
	strategy_fg_report_battery_status_changed(fg);

	fg_abnormal_temp_notify_mievent(fg);

	mca_log_err(
		"UISOC:%d CURRENT:%d RSOC:%d VOLTAGE:%d TEMP:%d PVBAT:%d PIBAT:%d PTBAT:%d HEALTH:%d\n",
		fg->batt_ui_soc, fg->batt_current, fg->batt_rsoc,
		fg->batt_voltage, fg->batt_temperature, fg->pack_vbat,
		fg->pack_ibat, fg->pack_tbat, fg->batt_health);
	mca_log_info("CC:%d S:%d RM:%d F:%d RAWSOC:%d, DODCOUNT:%d\n",
		     fg->batt_cyclecount, fg->batt_soh, fg->batt_rm,
		     fg->batt_fcc, fg->batt_raw_soc, fg->dod_count);

	return 0;
}

static void strategy_fg_adjust_interval(struct strategy_fg *fg)
{
	if (fg->update_period != STRATEGY_FG_UPDATE_PERIOD_NONE) {
		fg->update_interval = fg->update_period;
		return;
	}

	(void)strategy_class_fg_ops_get_soc();

	if (fg->near_vterm) {
		fg->update_interval = STRATEGY_FG_WORK_INTERVAL_FAST;
	} else if (fg->chg_status == POWER_SUPPLY_STATUS_CHARGING) {
		if (fg->fast_charge)
			fg->update_interval = STRATEGY_FG_WORK_INTERVAL_FAST;
		else
			fg->update_interval = STRATEGY_FG_WORK_INTERVAL_NORMAL;
	} else {
		if (fg->screen_status)
			fg->update_interval =
				STRATEGY_FG_WORK_INTERVAL_SCREEN_ON;
		else
			fg->update_interval =
				STRATEGY_FG_WORK_INTERVAL_SCREEN_OFF;

		if (fg->batt_voltage_mean <
		    (fg->vcutoff_shutdown_delay + BATT_LOW_VOLT_HY))
			fg->update_interval =
				STRATEGY_FG_WORK_INTERVAL_LOW_VOLT;

		if (fg->batt_temperature < BATT_TEMP_COLD_TH) {
			if (fg->batt_voltage_mean <
			    (fg->vcutoff_shutdown_delay + BATT_LOW_VOLT_HY))
				fg->update_interval =
					STRATEGY_FG_WORK_INTERVAL_LOW_TEMP_FAST;
			else
				fg->update_interval =
					STRATEGY_FG_WORK_INTERVAL_LOW_TEMP;
		}
	}

	if (fg->batt_voltage < (fg->vcutoff_sw + BATT_LOW_VOLT_SW_HY))
		fg->update_interval = STRATEGY_FG_WORK_INTERVAL_FAST;
}

static void strategy_fg_update_batt_dc(struct strategy_fg *fg)
{
	int dc_master;
	int dc_slave;
	int ret;

	if (fg->cfg.design_capacity > 0) {
		fg->batt_dc = fg->cfg.design_capacity * 1000;
		return;
	}

	ret = platform_fg_ops_get_full_design(FG_IC_MASTER, &dc_master);
	if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX)
		ret |= platform_fg_ops_get_full_design(FG_IC_SLAVE, &dc_slave);
	if (ret)
		return;

	if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX)
		fg->batt_dc = dc_master + dc_slave;
	else
		fg->batt_dc = dc_master;
}

void strategy_fg_update_batt_info(struct strategy_fg *fg)
{
	strategy_fg_update_batt_dc(fg);
	strategy_fg_update_cycle(fg);
	strategy_fg_update_rm(fg);
}

static void strategy_fg_monitor_workfunc(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct strategy_fg *fg =
		container_of(work, struct strategy_fg, monitor_work.work);
	if (fg && READ_ONCE(fg->stopping))
		return;

	const struct mca_hwid *hwid = mca_get_hwid_info();
	int ret;
	bool prohibit_jump = true;
	int flag;
	bool master_ok, slave_ok;

	if (!fg->fg_init_flag) {
		flag = platform_fg_ops_probe_ok(FG_IC_MASTER, &master_ok);
		if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX)
			flag |= platform_fg_ops_probe_ok(FG_IC_SLAVE,
							 &slave_ok);
		if (flag) {
			mca_queue_delayed_work(&fg->monitor_work,
					       msecs_to_jiffies(1000));
			return;
		}
		prohibit_jump = false;
		strategy_fg_update_batt_info(fg);
		if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX)
			fg->fg_init_flag = master_ok && slave_ok;
		else
			fg->fg_init_flag = master_ok;
		if (hwid && hwid->country_version == CountryCN &&
		    fg->fg_init_flag && fg->cfg.support_fl4p0)
			platform_fg_ops_fl4p0_enable_check(FG_IC_MASTER);
	}

	if (fg->resume_update_soc) {
		prohibit_jump = false;
		fg->resume_update_soc = false;
	}

	if (fg->boot_update_soc) {
		prohibit_jump = false;
		fg->boot_update_soc = false;
		mca_log_info("boot_update_soc update is_eu_model done\n");
	}

	if (!fg->voter_ok)
		strategy_fg_init_voter(fg);

	ret = fg_update_status(fg, prohibit_jump);
	strategy_fg_update_qbg_chg_data(fg);
	mca_strategy_check_termination(fg);
	if (fg->cfg.base_flip_same) {
		int cold_zone = 0;

		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_JEITA,
			STRATEGY_STATUS_TYPE_JEITA_COLD_ZONE, &cold_zone);
		fg->cold_zone = cold_zone;
	}
	mca_strategy_force_fw_report_full(fg);
	charge_slowly_monitor_func(fg);
	mca_strategy_check_recharge(fg);
	if (hwid && hwid->country_version == CountryCN && fg->cfg.support_fl4p0)
		mca_strategy_check_fl4p0_status(fg);
	if (fg->cfg.support_lossless_rechg)
		mca_strategy_check_lossless_recharge(fg);
	fg_check_batt_auth(fg);

	strategy_fg_adjust_interval(fg);
	mca_queue_delayed_work(&fg->monitor_work,
			       msecs_to_jiffies(fg->update_interval));
}

static void strategy_fg_dtpt_monitor_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct strategy_fg *fg =
		container_of(work, struct strategy_fg, dtpt_monitor_work.work);
	if (fg && READ_ONCE(fg->stopping))
		return;

	int master_isc_alert = 0, slave_isc_alert = 0, master_soa_alert = 0,
	    slave_soa_alert = 0;
	int interval = STRATEGY_FG_WORK_INTERVAL_DTPT;
	static int last_dtpt_status;
	int dtpt_status = 0;
	int ret;

	ret = platform_fg_ops_get_isc_alert_level(FG_IC_MASTER,
						  &master_isc_alert);
	ret |= platform_fg_ops_get_soa_alert_level(FG_IC_MASTER,
						   &master_soa_alert);
	if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX) {
		ret |= platform_fg_ops_get_isc_alert_level(FG_IC_SLAVE,
							   &slave_isc_alert);
		ret |= platform_fg_ops_get_soa_alert_level(FG_IC_SLAVE,
							   &slave_soa_alert);
	}

	master_isc_alert = max(master_isc_alert, slave_isc_alert);
	master_soa_alert = max(master_soa_alert, slave_soa_alert);

	mca_log_info("isc_alert = %d, soa_alert = %d\n", master_isc_alert,
		     master_soa_alert);

	if (master_isc_alert == 3 || master_soa_alert)
		dtpt_status = 1;
	else
		dtpt_status = 0;

	if (dtpt_status != last_dtpt_status) {
		last_dtpt_status = dtpt_status;
		mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
				       MCA_EVENT_BATTERY_DTPT, &dtpt_status);
	}

	schedule_delayed_work(&fg->dtpt_monitor_work,
			      msecs_to_jiffies(interval));
}

static void strategy_fl4p0_calibration_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct strategy_fg *fg = container_of(work, struct strategy_fg,
					      fl4p0_calibration_work.work);
	if (fg && READ_ONCE(fg->stopping))
		return;

	int supplement_energy, charge_energy;

	if (fg->charging_done)
		return;

	platform_fg_ops_get_real_supplement_energy(FG_IC_MASTER,
						   &supplement_energy);
	platform_fg_ops_get_calibration_charge_energy(FG_IC_MASTER,
						      &charge_energy);
	mca_log_info("supplement_energy = %d, charge_energy = %d",
		     supplement_energy, charge_energy);

	if (supplement_energy >= charge_energy || supplement_energy == 0xFFFF ||
	    fg->batt_temperature < fg->calibration_temp - 50 ||
	    fg->batt_temperature > fg->calibration_temp + 50) {
		mca_log_info("ok to calibration iterm");
		strategy_class_fg_ops_set_charging_done(true);
		return;
	}

	schedule_delayed_work(&fg->fl4p0_calibration_work,
			      msecs_to_jiffies(5000));
}

static void strategy_force_report_full_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct strategy_fg *fg = container_of(work, struct strategy_fg,
					      force_report_full_work.work);
	if (fg && READ_ONCE(fg->stopping))
		return;

	static int count = 0;

	if (!fg->power_present || !fg->fast_charge || fg->charging_done) {
		count = 0;
		return;
	}

	if (fg->batt_rsoc != 100) {
		platform_fg_ops_set_force_report_full(FG_IC_MASTER);
		mca_log_info(
			"fg not report full, send force_report_full cmd again...");
	} else {
		count = 0;
		mca_log_info("fg check report full ok");
		return;
	}

	if (++count < FG_FORCE_REPORT_FULL_TIMES)
		schedule_delayed_work(&fg->force_report_full_work,
				      msecs_to_jiffies(8000));
	else
		count = 0;
}

#define NVT1000_OTA_UPDATE_FLAG 0xAA55AA55
#define NVT1000_OTA_MONITOR_INTERVAL 5000
#define NVT1000_OTA_SOC_THR 59
#define NVT1000_OTA_TEMP_THR 100
#define NVT1000_OTA_CURR_MIN 100
#define NVT1000_OTA_CURR_MAX 1000
#define NVT1000_OTA_TRIGGER_COUNT 9

static void strategy_nvt1000_ota_monitor_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct strategy_fg *fg =
		container_of(work, struct strategy_fg, ota_update_work.work);
	if (fg && READ_ONCE(fg->stopping))
		return;

	static int update_count;
	union power_supply_propval pval = { 0 };
	int ota_flag = 0;
	int average_current = 0;
	bool cid_status = 0;
	int event_val;

	if (fg->batt_psy) {
		power_supply_get_property(fg->batt_psy, POWER_SUPPLY_PROP_STATUS,
					  &pval);
		fg->chg_status = pval.intval;
	}

	if (fg->chg_status == POWER_SUPPLY_STATUS_FULL ||
	    fg->chg_status == POWER_SUPPLY_STATUS_CHARGING) {
		mca_log_err("do not ota when charging\n");
		update_count = 0;
		return;
	}

	platform_fg_ops_get_ota_update_flag(FG_IC_MASTER, &ota_flag);
	if (ota_flag != NVT1000_OTA_UPDATE_FLAG) {
		mca_log_err("already ota update done\n");
		update_count = 0;
		return;
	}

	platform_fg_ops_get_average_current(FG_IC_MASTER, &average_current);
	protocol_class_pd_get_cid_status(FG_IC_MASTER, &cid_status);

	if (fg->batt_rsoc > NVT1000_OTA_SOC_THR &&
	    fg->batt_temperature >= NVT1000_OTA_TEMP_THR &&
	    average_current >= NVT1000_OTA_CURR_MIN &&
	    average_current <= NVT1000_OTA_CURR_MAX && !cid_status)
		update_count++;
	else
		update_count = 0;

	mca_log_err(
		"cid_status = %d, batt_rsoc = %d, batt_temperature = %d, average_current = %d, update_count = %d",
		cid_status, fg->batt_rsoc, fg->batt_temperature,
		average_current, update_count);

	if (update_count >= NVT1000_OTA_TRIGGER_COUNT) {
		mca_log_err("start nvt1000 ota\n");
		event_val = 1;
		mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
				       MCA_EVENT_FG_OTA_PROCESS, &event_val);
		platform_fg_ops_ota_update_check(FG_IC_MASTER);
		event_val = 0;
		mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
				       MCA_EVENT_FG_OTA_PROCESS, &event_val);
		mca_log_err("end nvt1000 ota\n");
		return;
	}

	queue_delayed_work(system_wq, &fg->ota_update_work,
			   msecs_to_jiffies(NVT1000_OTA_MONITOR_INTERVAL));
}

static int strategy_fg_get_parallel_rsoc(struct strategy_fg *fg, int *rsoc)
{
	int master_fcc;
	int slave_fcc;
	int master_weight;
	int slave_weight;
	int ret;

	ret = platform_fg_ops_get_fcc(FG_IC_MASTER, &master_fcc);
	ret |= platform_fg_ops_get_fcc(FG_IC_SLAVE, &slave_fcc);
	if (ret)
		return -1;

	master_weight = master_fcc * 100 / (master_fcc + slave_fcc);
	slave_weight = 100 - master_weight;
	ret = platform_fg_ops_get_rsoc(FG_IC_MASTER,
				       &(fg->master_batt_info.rsoc));
	ret |= platform_fg_ops_get_rsoc(FG_IC_SLAVE,
					&(fg->slave_batt_info.rsoc));
	if (ret)
		return -1;

	*rsoc = (fg->master_batt_info.rsoc * master_weight +
		 fg->slave_batt_info.rsoc * slave_weight + 50) /
		100;
	return 0;
}

static int strategy_fg_get_series_rsoc(struct strategy_fg *fg)
{
	return 0;
}

static int strategy_fg_ops_get_rsoc(void *data, int *rsoc)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	int ret = 0;

	if (!fg || !fg->fg_init_flag)
		return -1;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		ret = platform_fg_ops_get_rsoc(FG_IC_MASTER, rsoc);
		break;
	case MCA_FG_TYPE_PARALLEL:
		ret = strategy_fg_get_parallel_rsoc(fg, rsoc);
		break;
	case MCA_FG_TYPE_SERIES:
		ret = strategy_fg_get_series_rsoc(fg);
		break;
	default:
		return -1;
	}

	return ret;
}

static int strategy_fg_ops_get_soc(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg)
		return STRATEGY_FG_ERROR_FAKE_SOC;

	if (fg->fake_soc != STRATEGY_FG_FAKE_SOC_NONE)
		return fg->fake_soc;

	if (!fg->fg_init_flag)
		return STRATEGY_FG_ERROR_FAKE_SOC;

	return fg->batt_ui_soc;
}

static int strategy_fg_ops_get_temp(void *data, int *temp)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		*temp = STRATEGY_FG_ERROR_FAKE_TEMP;
	else
		*temp = fg->batt_temperature;

	return 0;
}

static int strategy_fg_ops_get_thermal_temp(void *data, int *temp)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		*temp = STRATEGY_FG_ERROR_FAKE_TEMP;
	else if (fg->thermal_temp_select == 0)
		*temp = fg->batt_temperature;
	else
		*temp = fg->original_temp;

	return 0;
}

static int strategy_fg_ops_get_model_name(void *data, const char **model_name)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	const struct mca_hwid *hwid = mca_get_hwid_info();
	const char *device_name = NULL;

	if (!fg || !fg->fg_init_flag)
		return -1;

	if (!strcmp(fg->cfg.model_name, fg->cfg.model_name_gl)) {
		*model_name = fg->cfg.model_name;
		goto country_select;
	}

	platform_fg_ops_get_device_name(FG_IC_MASTER, &device_name);
	if (device_name) {
		if (!strncmp(device_name, "BP4U", 4)) {
			*model_name = fg->cfg.model_name;
			mca_log_info("project O3 matched BP4U, set cn model name\n");
			goto country_select;
		}
		if (!strncmp(device_name, "BP4V", 4) ||
		    !strcmp("5XM31", device_name) ||
		    !strncmp("5@BP", device_name, 4)) {
			*model_name = fg->cfg.model_name_gl;
			mca_log_info("Not cn project, set global model name\n");
			goto country_select;
		}
	}

	*model_name = fg->cfg.model_name;

country_select:
	if (!fg->cfg.show_model_by_country)
		return 0;

	switch (hwid->country_version) {
	case CountryGlobal:
		*model_name = fg->cfg.model_name_gl;
		break;
	case CountryIndia:
		*model_name = fg->cfg.model_name_in;
		break;
	case CountryCN:
	default:
		*model_name = fg->cfg.model_name;
		break;
	}

	return 0;
}

static int strategy_fg_get_parallel_curr(struct strategy_fg *fg, int *curr)
{
	int ret;

	ret = platform_fg_ops_get_curr(FG_IC_SLAVE,
				       &(fg->slave_batt_info.curr));
	ret |= platform_fg_ops_get_curr(FG_IC_MASTER,
					&(fg->master_batt_info.curr));
	if (ret)
		return -1;

	*curr = fg->slave_batt_info.curr + fg->master_batt_info.curr;

	return 0;
}

static int strategy_fg_get_series_curr(struct strategy_fg *fg, int *curr)
{
	return 0;
}

static int strategy_fg_ops_get_curr(void *data, int *curr)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag) {
		*curr = STRATEGY_FG_ERROR_FAKE_CURR;
		return 0;
	}

	if (fg->update_interval == STRATEGY_FG_WORK_INTERVAL_FAST) {
		*curr = fg->batt_current;
		return 0;
	}

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
	case MCA_FG_TYPE_SINGLE_SERIES:
		return platform_fg_ops_get_curr(FG_IC_MASTER, curr);
	case MCA_FG_TYPE_PARALLEL:
		return strategy_fg_get_parallel_curr(fg, curr);
	case MCA_FG_TYPE_SERIES:
		return strategy_fg_get_series_curr(fg, curr);
	default:
		return -1;
	}

	*curr = fg->batt_current;
	return 0;
}

static int strategy_fg_ops_get_volt(void *data, int *volt)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag) {
		*volt = STRATEGY_FG_ERROR_FAKE_VOLT;
		return 0;
	}

	strategy_fg_update_batt_volt(fg);
	*volt = fg->batt_voltage;

	return 0;
}

static int strategy_fg_ops_get_cycle(void *data, int *cycle)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*cycle = fg->batt_cyclecount;

	return 0;
}

int strategy_lossless_rechg_parse_dt(struct strategy_fg *fg)
{
	struct device_node *node = fg->dev->of_node;
	int ret = 0;
	int idata[3] = { 0 };

	ret = mca_parse_dts_u32(node, "support-lossless-rechg",
				&(fg->cfg.support_lossless_rechg), 0);
	if (ret < 0) {
		mca_log_err("parse support-lossless-rechg failed %d\n", ret);
		return ret;
	}
	if (fg->cfg.support_lossless_rechg) {
		ret = mca_parse_dts_u32_array(node, "lossless_rechg_cfg", idata,
					      3);
		if (ret < 0) {
			mca_log_err("parse lossless-rechg_cfg failed %d\n",
				    ret);
			return ret;
		}

		fg->cfg.rechg_cfg.temp_low = idata[0];
		fg->cfg.rechg_cfg.temp_high = idata[1];
		fg->cfg.rechg_cfg.rest_time = idata[2];
	}

	return ret;
}

int strategy_full_design_parse_dt(struct strategy_fg *fg)
{
	struct device_node *node = fg->dev->of_node;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	fg->support_full_design_gl =
		of_property_read_bool(node, "support_full_design_gl");
	if (hwid && fg->support_full_design_gl &&
	    hwid->country_version != CountryCN) {
		(void)mca_parse_dts_u32(node, "charge-full-design-gl",
					&(fg->cfg.design_capacity),
					DEFAULT_DESIGN_CAPACITY);
	} else {
		(void)mca_parse_dts_u32(node, "charge-full-design",
					&(fg->cfg.design_capacity),
					DEFAULT_DESIGN_CAPACITY);
	}

	return 0;
}

static int strategy_fg_parse_extreme_cold_dt(struct strategy_fg *fg)
{
	struct device_node *node = fg->dev->of_node;
	int row, col, len;
	int data[FG_EXTREME_COLD_MAX_GROUP * FG_EXTREME_COLD_SIZE] = { 0 };

	len = mca_parse_dts_string_array(node, "extreme_cold_temp_compemsation",
					 data, FG_EXTREME_COLD_MAX_GROUP,
					 FG_EXTREME_COLD_SIZE);
	if (len < 0) {
		mca_log_err("parse extreme_cold_temp_compemsation failed\n");
		return -1;
	}

	fg->cfg.extreme_cold_para_size = len / FG_EXTREME_COLD_SIZE;
	for (row = 0; row < fg->cfg.extreme_cold_para_size; row++) {
		col = row * FG_EXTREME_COLD_SIZE +
		      FG_EXTREME_COLD_CYCLE_COUNT_MAX;
		fg->cfg.extreme_cold_para[row].cycle_count_max = data[col];
		col = row * FG_EXTREME_COLD_SIZE + FG_EXTREME_COLD_TEMP_MAX;
		fg->cfg.extreme_cold_para[row].temp_max = data[col];
		col = row * FG_EXTREME_COLD_SIZE + FG_EXTREME_COLD_COMPENSATION;
		fg->cfg.extreme_cold_para[row].compensation = data[col];
		mca_log_err("extreme_cold_para[%d]: %d, %d, %d\n", row,
			    fg->cfg.extreme_cold_para[row].cycle_count_max,
			    fg->cfg.extreme_cold_para[row].temp_max,
			    fg->cfg.extreme_cold_para[row].compensation);
	}

	return 0;
}

#define TEMP_PARA_MAX_GROUP 5
enum mca_quick_charge_temp_para_ele {
	MCA_FG_TEMP = 0,
	MCA_FG_TEMP_HYS,
	MCA_FG_NORMAL_ITERM,
	MCA_FG_FFC_ITERM,
	MCA_FG_ITERM_PARA_MAX,
};

static int strategy_parse_term_curr_para(struct device_node *node,
					 int batt_role, const char *name,
					 struct strategy_fg *fg)
{
	struct term_curr_para *iterm_para =
		&(fg->cfg.term_curr_data[batt_role]);
	int array_len, row, col, i;
	const char *tmp_string = NULL;
	struct term_curr_data *iterm_data = NULL;

	array_len = mca_parse_dts_count_strings(node, name, TEMP_PARA_MAX_GROUP,
						MCA_FG_ITERM_PARA_MAX);
	if (array_len < 0) {
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	iterm_para->iterm_para_size = array_len / MCA_FG_ITERM_PARA_MAX;
	iterm_para->iterm_data = kcalloc(iterm_para->iterm_para_size,
					 sizeof(*iterm_data), GFP_KERNEL);
	if (!iterm_para->iterm_data) {
		mca_log_err("iterm para no mem\n");
		return -ENOMEM;
	}
	iterm_data = iterm_para->iterm_data;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		row = i / MCA_FG_ITERM_PARA_MAX;
		col = i % MCA_FG_ITERM_PARA_MAX;
		switch (col) {
		case MCA_FG_TEMP:
			if (kstrtoint(tmp_string, 10,
				      &iterm_data[row].batt_temp))
				goto error;
			break;
		case MCA_FG_TEMP_HYS:
			if (kstrtoint(tmp_string, 10,
				      &iterm_data[row].batt_temp_offset))
				goto error;
			break;
		case MCA_FG_NORMAL_ITERM:
			if (kstrtoint(tmp_string, 10,
				      &iterm_data[row].normal_iterm))
				goto error;
			break;
		case MCA_FG_FFC_ITERM:
			if (kstrtoint(tmp_string, 10,
				      &iterm_data[row].ffc_iterm))
				goto error;
			break;
		default:
			break;
		}
	}

	for (i = 0; i < iterm_para->iterm_para_size; i++)
		mca_log_err("iterm para %d %d %d %d\n", iterm_data[i].batt_temp,
			    iterm_data[i].batt_temp_offset,
			    iterm_data[i].normal_iterm,
			    iterm_data[i].ffc_iterm);

	return 0;
error:
	kfree(iterm_data);
	iterm_data = NULL;
	return -1;
}

enum mca_fg_iterm_para_ele {
	MCA_FG_BATT_ROLE = 0,
	MCA_FG_BATT_PARA_NAME,
	MCA_FG_BATT_PARA_MAX,
};
#define ITERM_PARA_MAX_GROUP 3

static int strategy_term_curr_parse_dt(struct strategy_fg *fg)
{
	struct device_node *node = fg->dev->of_node;
	int array_len, row, col, i;
	int batt_role = 0;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(
		node, "iterm_para", ITERM_PARA_MAX_GROUP, MCA_FG_BATT_PARA_MAX);
	if (array_len < 0) {
		mca_log_err("parse batt_para failed\n");
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "iterm_para", i,
					       &tmp_string))
			return -1;

		row = i / MCA_FG_BATT_PARA_MAX;
		col = i % MCA_FG_BATT_PARA_MAX;
		switch (col) {
		case MCA_FG_BATT_ROLE:
			if (kstrtoint(tmp_string, 10, &batt_role))
				return -1;
			if (batt_role > FG_IC_SLAVE)
				i += 1;
			break;
		case MCA_FG_BATT_PARA_NAME:
			if (strategy_parse_term_curr_para(node, batt_role,
							  tmp_string, fg))
				return -1;
			break;
		default:
			break;
		}
	}

	if (!fg->cfg.term_curr_data[FG_IC_MASTER].iterm_data) {
		mca_log_err("parse master iterm para failed\n");
		return -1;
	}

	if (!fg->cfg.term_curr_data[FG_IC_SLAVE].iterm_data) {
		mca_log_err("parse slave iterm para failed\n");
		return -1;
	}

	return 0;
}

static int strategy_fg_parse_dt(struct strategy_fg *fg)
{
	struct device_node *node = fg->dev->of_node;
	int ret = 0;
	int len;
	int i, j;
	int soc_decimal[SOC_DECIMAL_MAX_LEVEL * SOC_DECIMAL_MAX] = { 0 };

	if (!node) {
		mca_log_err("No DT data Failing Probe\n");
		return -EINVAL;
	}

	ret = mca_parse_dts_u32(node, "report-full-rsoc",
				&(fg->cfg.report_full_raw_soc),
				STRATEGY_FG_REPORT_FULL_RSOC);
	ret |= mca_parse_dts_u32(node, "soc-proportion",
				 &(fg->cfg.soc_proportion),
				 STRATEGY_FG_SOC_PROPORTION);
	ret |= mca_parse_dts_u32(node, "soc-proportion-c",
				 &(fg->cfg.soc_proportion_c),
				 STRATEGY_FG_SOC_PROPORTION_C);
	ret |= mca_parse_dts_u32(node, "fg_type", &(fg->cfg.fg_type),
				 MCA_FG_TYPE_SINGLE);
	ret |= mca_parse_dts_u32(node, "adapt_power", &(fg->cfg.adapt_power),
				 STRATEGY_FG_ADAPT_POWER);
	ret |= mca_parse_dts_u32(node, "support_dtpt", &(fg->cfg.support_dtpt),
				 STRATEGY_FG_SUPPORT_DTPT);
	ret |= mca_parse_dts_u32(node, "fg_hightemp_vterm",
				 &(fg->cfg.fg_hightemp_vterm),
				 STRATEGY_FG_DEFAULT_HIGH_TEMP_VTERM);
	ret |= mca_parse_dts_u32(node, "terminated_by_cp",
				 &(fg->cfg.terminated_by_cp),
				 STRATEGY_FG_TERMINATED_BY_CP);
	ret |= mca_parse_dts_string(node, "model-name", &(fg->cfg.model_name));
	ret |= mca_parse_dts_string(node, "model-name-global",
				    &(fg->cfg.model_name_gl));
	ret |= mca_parse_dts_string(node, "model-name-in",
				    &(fg->cfg.model_name_in));
	fg->cfg.show_model_by_country =
		!!of_find_property(node, "show-model-by-country-version", NULL);
	mca_parse_dts_u32(node, "support_nvt1000_ota",
			  &(fg->cfg.support_nvt1000_ota), 0);
	fg->cfg.support_qbg = !!of_find_property(node, "support-qbg", NULL);
	if (ret) {
		mca_log_err("strategy fg parse dt failed, ret=%d\n", ret);
	}

	mca_parse_dts_u32(node, "support_fl4p0", &(fg->cfg.support_fl4p0),
			  STRATEGY_FG_SUPPORT_FL4P0);
	if (fg->cfg.support_fl4p0) {
		len = mca_parse_dts_u32_count(node, "fl4p0_ffc_safe_iterm",
					      SAFE_ITERM_CYCLE_LEVEL,
					      SAFE_ITERM_TEMP_LEVEL);
		if (len < 0) {
			mca_log_err("parse fl4p0_ffc_safe_iterm failed\n");
			return -1;
		}
		ret = mca_parse_dts_u32_array(node, "fl4p0_ffc_safe_iterm",
					      &fg->cfg.ffc_safe_item[0][0],
					      len);
		if (ret < 0)
			return -1;
	}

	len = mca_parse_dts_u32_count(node, "soc_decimal",
				      SOC_DECIMAL_MAX_LEVEL, SOC_DECIMAL_MAX);
	if (len < 0) {
		mca_log_err("parse soc_decimal len failed\n");
		return -1;
	}
	ret = mca_parse_dts_u32_array(node, "soc_decimal", soc_decimal, len);
	if (ret < 0) {
		mca_log_err("parse soc_decimal cfg failed\n");
		return -1;
	}
	fg->cfg.soc_decimal_cnt = len / SOC_DECIMAL_MAX;
	for (i = 0; i < fg->cfg.soc_decimal_cnt; i++) {
		j = SOC_DECIMAL_MAX * i;
		fg->cfg.soc_decimal[i].soc = soc_decimal[j + SOC_DECIMAL_SOC];
		fg->cfg.soc_decimal[i].rate = soc_decimal[j + SOC_DECIMAL_RATE];
	}

	ret = mca_battery_shutdown_parse_dt(fg);
	if (ret < 0) {
		mca_log_err("parse battery shutdown failed %d\n", ret);
		return ret;
	}

	ret = strategy_lossless_rechg_parse_dt(fg);
	if (ret < 0) {
		mca_log_err("parse lossless recharging failed %d\n", ret);
		return 0;
	}

	ret = strategy_full_design_parse_dt(fg);
	if (ret < 0) {
		mca_log_err("parse full design capacity dt failed %d\n", ret);
		return 0;
	}

	ret = strategy_soc_smooth_parse_dt(fg);
	if (ret < 0) {
		mca_log_err("parse soc smooth failed %d\n", ret);
		fg->smooth.suppot_RSOC_0_smooth = 0;
		return 0;
	}

	ret = strategy_fg_parse_extreme_cold_dt(fg);
	if (ret < 0) {
		mca_log_err("parse extreme cold dt failed %d\n", ret);
		return 0;
	}

	fg->cfg.support_base_flip =
		of_property_read_bool(node, "support-base-flip");
	fg->cfg.base_flip_same =
		of_property_read_bool(node, "base-flip-same");
	if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX) {
		ret = strategy_term_curr_parse_dt(fg);
		if (ret < 0) {
			mca_log_err("parse iterm failed %d\n", ret);
			return 0;
		}
	}

	return ret;
}

#define STRATEGY_FG_SOC_DECIMAL_DEFAULT_RATE 10
static int strategy_fg_ops_get_soc_decimal(void *data, int *soc_decimal,
					   int *rate)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	int i;

	if (!data)
		return -1;

	*rate = STRATEGY_FG_SOC_DECIMAL_DEFAULT_RATE;
	for (i = fg->cfg.soc_decimal_cnt - 1; i >= 0; i--) {
		if (fg->batt_ui_soc > fg->cfg.soc_decimal[i].soc) {
			*rate = fg->cfg.soc_decimal[i].rate;
			break;
		}
	}
	// make sure the decimal growth does not exceed 99
	*soc_decimal = fg->batt_ui_soc_decimal * (100 - *rate) / 100 - 1;

	return 0;
}

static bool strategy_fg_ops_get_charging_done(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return false;

	if (!fg || !fg->fg_init_flag)
		return -1;

	return fg->charging_done;
}

static int strategy_fg_ops_set_charging_done(void *data, bool charging_done)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	mca_log_info("set charging_done: %d\n", charging_done);
	if (charging_done) {
		mca_vote(fg->en_voter, "term_recharge", true, 0);
		mca_vote(fg->charge_limit_voter, "qc_done", false, 0);
		fg->charging_done = true;
		fg->recharging = false;
		fg->lossless_recharge = false;
		fg->near_vterm = false;
		fg->ffc_continue_charge = false;
		if (fg->is_eu_model) {
			if (fg->batt_rsoc < 100) {
				fg->en_smooth_full = true;
				mca_log_info(
					"charging_done start en_smooth_full\n");
			} else {
				fg->en_smooth_full = false;
				mca_log_info(
					"charging_done stop en_smooth_full\n");
			}
			fg->keep_full_flag = false;
		} else {
			fg->keep_full_flag = true;
		}
		if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX)
			strategy_fg_reset_co_to_default(fg);
	} else {
		fg->charging_done = false;
		fg->en_smooth_full = false;
	}

	return 0;
}

static int strategy_fg_check_battery_adapt_power(void *data, int *match)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	int ret = 0;
	int adapt_power = 0, adapt_power_slave = 0;

	if (fg->fake_bap_match >= 0) {
		*match = fg->fake_bap_match;
	} else {
		if (fg->cfg.fg_type <= MCA_FG_TYPE_SINGLE_NUM_MAX) {
			ret = platform_fg_ops_get_adapt_power(FG_IC_MASTER,
							      &adapt_power);
		} else {
			ret = platform_fg_ops_get_adapt_power(FG_IC_MASTER,
							      &adapt_power);
			ret |= platform_fg_ops_get_adapt_power(
				FG_IC_SLAVE, &adapt_power_slave);
			if (fg->cfg.support_base_flip) {
				if (adapt_power == 55 &&
				    adapt_power_slave == 15)
					adapt_power = 67;
			} else {
				if (adapt_power != adapt_power_slave)
					adapt_power = -1;
			}
		}
		mca_log_info("check battery adapt_power: %d, read value %d\n",
			     fg->cfg.adapt_power, adapt_power);
		*match = fg->cfg.adapt_power == adapt_power;
	}
	mca_log_info("%s: fake_bap_match: %d, match %d\n", __func__,
		     fg->fake_bap_match, *match);

	return ret;
}

static int strategy_fg_ops_set_fastcharge(void *data, bool en)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	int ret;

	if (!fg || !fg->fg_init_flag)
		return -1;

	ret = platform_fg_ops_set_fastcharge(FG_IC_MASTER, en);
	if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX)
		ret |= platform_fg_ops_set_fastcharge(FG_IC_SLAVE, en);

	if (!en && fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX &&
	    mca_is_override_vote_enabled(fg->vterm_voter)) {
		mca_vote_override(fg->vterm_voter, "para_term", false, 0);
		mca_log_err("fastcharge off, clean para_term override\n");
	}

	return ret;
}

static int strategy_fg_ops_get_fastcharge(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return 0;

	if (fg->update_interval == STRATEGY_FG_WORK_INTERVAL_FAST)
		return fg->fast_charge;

	return strategy_fg_get_fast_charge(fg);
}

static int strategy_fg_ops_get_authentic(void *data, bool *authentic)
{
	int master_auth = 1;
	int slave_auth = 1;
	int ret;
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	ret = platform_fg_ops_get_authentic(FG_IC_MASTER, &master_auth);
	if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX)
		ret |= platform_fg_ops_get_authentic(FG_IC_SLAVE, &slave_auth);

	if (ret)
		return ret;

	*authentic = (master_auth && slave_auth);

	return 0;
}

static int strategy_fg_ops_get_dc(void *data, int *dc)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*dc = fg->batt_dc;

	return 0;
}

static int strategy_fg_ops_get_rm(void *data, int *rm)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*rm = fg->batt_rm;

	return 0;
}

static int strategy_fg_ops_get_fcc(void *data, int *fcc)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*fcc = fg->batt_fcc;

	return 0;
}

static int strategy_fg_ops_get_health(void *data, int *health)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*health = fg->batt_health;
	return 0;
}

static int strategy_fg_ops_get_soh(void *data, int *soh)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*soh = fg->batt_soh;
	return 0;
}

int strategy_fg_ops_is_init_ok(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	return fg->fg_init_flag;
}

int strategy_fg_ops_is_chip_ok(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	return fg->fg_init_flag && !fg->fg_error;
}
int strategy_fg_dual_ops_is_chip_ok(void *data, int index)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (index == FG_IC_SLAVE) {
		if (!fg->dual_present[FG_IC_SLAVE])
			return false;
		return !fg->dual_error[FG_IC_SLAVE];
	}

	if (index != FG_IC_MASTER || !fg->dual_present[FG_IC_MASTER])
		return false;

	return !fg->dual_error[FG_IC_MASTER];
}

int strategy_fg_ops_get_pack_vendor_id(void *data, int *vendor_id)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	return platform_fg_ops_get_pack_vendor(FG_IC_MASTER, vendor_id);
}

static int strategy_fg_ops_get_first_termiation(void *data, int *value)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*value = fg->first_termination;
	return 0;
}

static int strategy_fg_ops_get_temp_offset_flag(void *data, int *flag)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (!fg)
		return -1;
	if (!fg->support_battery_date)
		return -1;

	if (fg->temp_offset_force)
		*flag = 1;
	else
		*flag = fg->temp_offset_flag_val;

	return 0;
}

static int strategy_fg_get_status(int status, void *value, void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	int match = 0;

	if (!value || !fg)
		return -1;

	switch (status) {
	case STRATEGY_STATUS_TYPE_FG_FIRST_TERM:
		*(int *)value = fg->first_termination;
		return 0;
	case STRATEGY_STATUS_TYPE_FG_BATT_MATCH:
		strategy_fg_check_battery_adapt_power(fg, &match);
		*(int *)value = match & fg->batt_auth;
		return 0;
	default:
		return -1;
	}
}

static void mca_strategy_get_manufacturing_date(struct strategy_fg *fg,
						char *date)
{
	char date_master[9] = { 0 };
	char date_slave[9] = { 0 };

	if (!fg->support_battery_date)
		return;

	if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX) {
		platform_fg_ops_get_manufacturing_date(FG_IC_MASTER,
						       (u8 *)date_master);
		platform_fg_ops_get_manufacturing_date(FG_IC_SLAVE,
						       (u8 *)date_slave);
		mca_log_err("manufacturing_date master:%s, slave:%s\n",
			    date_master, date_slave);

		if (date_slave[0] && memcmp(date_slave, date_master, 8) > 0)
			memcpy(date, date_slave, 8);
		else
			memcpy(date, date_master, 8);
	} else {
		platform_fg_ops_get_manufacturing_date(FG_IC_MASTER, (u8 *)date);
	}
}

static void mca_strategy_get_first_usage_date(struct strategy_fg *fg,
					      char *date)
{
	char date_master[9] = { 0 };
	char date_slave[9] = { 0 };
	bool master_valid, slave_valid;

	if (!fg->support_battery_date)
		return;

	if (fg->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX) {
		platform_fg_ops_get_first_usage_date(FG_IC_MASTER,
						     (u8 *)date_master);
		platform_fg_ops_get_first_usage_date(FG_IC_SLAVE,
						     (u8 *)date_slave);

		master_valid = strncmp(date_master, "99999999", 8) != 0;
		slave_valid = strncmp(date_slave, "99999999", 8) != 0;

		if (master_valid && slave_valid)
			memcpy(date,
			       memcmp(date_slave, date_master, 8) > 0 ?
				       date_slave :
				       date_master,
			       8);
		else if (slave_valid)
			memcpy(date, date_slave, 8);
		else
			memcpy(date, date_master, 8);
	} else {
		platform_fg_ops_get_first_usage_date(FG_IC_MASTER, (u8 *)date);
	}
}

static void mca_strategy_set_first_usage_date(struct strategy_fg *fg,
					      const char *date)
{
	if (!fg->support_battery_date)
		return;

	if (fg->cfg.fg_type == MCA_FG_TYPE_PARALLEL) {
		platform_fg_ops_set_first_usage_date(FG_IC_MASTER, date);
		platform_fg_ops_set_first_usage_date(FG_IC_SLAVE, date);
	} else if (fg->cfg.fg_type == MCA_FG_TYPE_SINGLE) {
		platform_fg_ops_set_first_usage_date(FG_IC_MASTER, date);
	}
}

static int mca_strategy_fg_shutdown_cb(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	struct strategy_fg *fg =
		container_of(nb, struct strategy_fg, reboot_nb);
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	if (fg->cfg.support_qbg)
		mca_log_err("store uisoc: %d to onewire dev\n",
			    fg->batt_ui_soc);

	return NOTIFY_DONE;
}

static void strategy_batt_abnormal_dfx_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct strategy_fg *fg = container_of(to_delayed_work(work),
					      struct strategy_fg,
					      batt_abnormal_dfx_work);
	if (fg && READ_ONCE(fg->stopping))
		return;

	int info[10] = { 0 };
	u32 last_ocd = 0, last_hocd = 0;
	u32 last_cuv = 0, last_hcuv = 0;
	u32 last_hscd = 0;
	int report[7];

	platform_fg_ops_get_batt_abnormal_info(FG_IC_MASTER, info);
	charger_partition_read_ocd_count(&last_ocd, &last_hocd);
	charger_partition_read_cuv_count(&last_cuv, &last_hcuv);
	charger_partition_read_hscd_count(&last_hscd);

	mca_log_info(
		"last_ocd_count:%d, last_hocd_count:%d, last_cuv_count:%d, last_hcuv_count:%d, last_hscd_count:%d\n",
		last_ocd, last_hocd, last_cuv, last_hcuv, last_hscd);
	mca_log_info(
		"ocd_count:%d, hocd_count:%d, cuv_count:%d, hcuv_count:%d, hscd_count:%d\n",
		info[0], info[2], info[4], info[6], info[8]);

	if (last_ocd != info[0] || last_hocd != info[2]) {
		report[0] = info[0];
		report[1] = info[1];
		report[2] = info[2];
		report[3] = info[3];
		report[4] = fg->batt_ui_soc;
		report[5] = fg->batt_voltage;
		report[6] = fg->batt_temperature;
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_OCD, report, 7);
		charger_partition_write_ocd_count(info[0], info[2]);
	}

	if (last_cuv != info[4] || last_hcuv != info[6]) {
		report[0] = info[4];
		report[1] = info[5];
		report[2] = info[6];
		report[3] = info[7];
		report[4] = fg->batt_ui_soc;
		report[5] = fg->batt_voltage;
		report[6] = fg->batt_temperature;
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_CUV, report, 7);
		charger_partition_write_cuv_count(info[4], info[6]);
	}

	if (last_hscd != info[8]) {
		report[0] = info[8];
		report[1] = info[9];
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_HSCD, report, 2);
		charger_partition_write_hcsd_count(info[8]);
	}
}

static struct strategy_fg_class_ops g_strategy_fg_ops = {
	.strategy_fg_is_init_ok = strategy_fg_ops_is_init_ok,
	.strategy_fg_is_chip_ok = strategy_fg_ops_is_chip_ok,
	.strategy_fg_dual_is_chip_ok = strategy_fg_dual_ops_is_chip_ok,
	.strategy_fg_get_rsoc = strategy_fg_ops_get_rsoc,
	.strategy_fg_get_soc = strategy_fg_ops_get_soc,
	.strategy_fg_get_temp = strategy_fg_ops_get_temp,
	.strategy_fg_get_current = strategy_fg_ops_get_curr,
	.strategy_fg_get_voltage = strategy_fg_ops_get_volt,
	.strategy_fg_get_cycle = strategy_fg_ops_get_cycle,
	.strategy_fg_get_voltage_mean = strategy_fg_get_volt_mean,
	.strategy_fg_get_soc_decimal_info = strategy_fg_ops_get_soc_decimal,
	.strategy_fg_get_charging_done = strategy_fg_ops_get_charging_done,
	.strategy_fg_set_charging_done = strategy_fg_ops_set_charging_done,
	.strategy_fg_get_model_name = strategy_fg_ops_get_model_name,
	.strategy_fg_set_fastcharge = strategy_fg_ops_set_fastcharge,
	.strategy_fg_get_fastcharge = strategy_fg_ops_get_fastcharge,
	.strategy_fg_get_authentic = strategy_fg_ops_get_authentic,
	.strategy_fg_get_dc = strategy_fg_ops_get_dc,
	.strategy_fg_get_rm = strategy_fg_ops_get_rm,
	.strategy_fg_get_fcc = strategy_fg_ops_get_fcc,
	.strategy_fg_get_health = strategy_fg_ops_get_health,
	.strategy_fg_get_first_termination = strategy_fg_ops_get_first_termiation,
	.strategy_fg_get_pack_vendor_id = strategy_fg_ops_get_pack_vendor_id,
	.strategy_fg_get_thermal_temperature = strategy_fg_ops_get_thermal_temp,
	.strategy_fg_get_soh = strategy_fg_ops_get_soh,
	.strategy_fg_get_temp_offset_flag = strategy_fg_ops_get_temp_offset_flag,
};

static void delay_reset_full_flag_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct strategy_fg *fg = container_of(work, struct strategy_fg,
					      delay_reset_full_flag_work.work);
	if (fg && READ_ONCE(fg->stopping))
		return;

	if (!fg || !fg->fg_init_flag)
		return;

	fg->keep_full_flag = false;
}

#define EU_MODEL_RECHARGE_SOC 100
static int strategy_fg_process_event(int event, int value, void *data)
{
	struct strategy_fg *info = data;
	if (info && READ_ONCE(info->stopping))
		return -ESHUTDOWN;

	if (!data)
		return -1;

	mca_log_info("receive event %d, value %d", event, value);
	switch (event) {
	case MCA_EVENT_USB_CONNECT:
	case MCA_EVENT_WIRELESS_CONNECT:
		info->power_present = true;
		info->plugin_time = ktime_get_boottime_seconds();
		info->online_time = info->plugin_time;
		info->non_std_charger_reported = false;
		info->co_ctrl_support = true;
		info->co_ctrl_active = true;
		if (info->is_eu_model) {
			if (info->batt_ui_soc < EU_MODEL_RECHARGE_SOC) {
				mca_vote(info->en_voter, "term_recharge", false,
					 0);
				info->charging_done = false;
				mca_log_info(
					"usb connect clear charging_done\n");
			}
		}
		mca_log_info("plugin keep_full_flag: %d, plugin_time: %lld\n",
			     info->keep_full_flag, info->plugin_time);
		mca_mod_delayed_work(
			&info->monitor_work,
			msecs_to_jiffies(STRATEGY_FG_WORK_INTERVAL_FAST));
		mca_cancel_delayed_work_sync(&info->delay_reset_full_flag_work);
		if (info->cfg.support_dtpt)
			schedule_delayed_work(
				&info->dtpt_monitor_work,
				msecs_to_jiffies(
					STRATEGY_FG_WORK_INTERVAL_DTPT));
		if (info->cfg.support_nvt1000_ota)
			mca_cancel_delayed_work_sync(&info->ota_update_work);
		break;
	case MCA_EVENT_USB_DISCONNECT:
	case MCA_EVENT_WIRELESS_DISCONNECT:
		if (!info->is_eu_model) {
			mca_vote(info->en_voter, "term_recharge", false, 0);
			info->charging_done = false;
		} else {
			if (info->batt_ui_soc < EU_MODEL_RECHARGE_SOC) {
				mca_vote(info->en_voter, "term_recharge", false,
					 0);
				info->charging_done = false;
				mca_log_info(
					"usb disonnect clear charging_done\n");
			}
		}
		mca_vote(info->iterm_voter, "calibration", false, 0);
		mca_vote(info->charge_limit_voter, "qc_done", false, 0);
		info->recharging = false;
		info->power_present = false;
		info->en_smooth_full = false;
		info->near_vterm = false;

		mca_log_info("plugout keep_full_flag = %d\n",
			     info->keep_full_flag);
		if (info->keep_full_flag)
			schedule_delayed_work(&info->delay_reset_full_flag_work,
					      msecs_to_jiffies(60 * 1000));
		if (info->cfg.support_dtpt)
			mca_cancel_delayed_work_sync(&info->dtpt_monitor_work);
		if (info->cfg.fg_type > MCA_FG_TYPE_SINGLE_NUM_MAX) {
			mca_vote(info->vterm_voter, "losslessRec", false, 0);
			info->co_ctrl_active = false;
			info->fg1_batt_ctr_enabled = false;
			info->fg2_batt_ctr_enabled = false;
			info->monitor_soc_flag = false;
			info->first_termination = false;
			info->fg_lock_flag = false;
		}
		if (info->cfg.support_nvt1000_ota) {
			int ota_flag = 0;

			platform_fg_ops_get_ota_update_flag(FG_IC_MASTER,
							    &ota_flag);
			if (ota_flag == NVT1000_OTA_UPDATE_FLAG)
				queue_delayed_work(
					system_wq, &info->ota_update_work,
					msecs_to_jiffies(
						NVT1000_OTA_MONITOR_INTERVAL));
		}
		break;
	case MCA_EVENT_IS_EU_MODEL:
		if (!info->is_eu_model) {
			info->is_eu_model = value;
			info->boot_update_soc = value ? true : false;
			mca_log_info("is_eu_model[%d] update soc\n",
				     info->is_eu_model);
		}
		break;
	case MCA_EVENT_CHARGE_TYPE_CHANGE:
		info->real_type = value;
		break;
	case MCA_EVENT_BQ_FG_ERROR:
		info->fg_error = 1;
		fg_update_status(info, true);
		break;
	default:
		break;
	}

	return 0;
}

static void strategy_fg_init_voter(struct strategy_fg *fg)
{
	fg->voter_ok = false;

	fg->en_voter = mca_find_votable("chg_enable");
	if (!fg->en_voter)
		return;
	fg->vterm_voter = mca_find_votable("term_volt");
	if (!fg->vterm_voter)
		return;
	fg->iterm_voter = mca_find_votable("term_curr");
	if (!fg->iterm_voter)
		return;
	fg->charge_limit_voter = mca_find_votable("buck_charge_curr");
	if (!fg->charge_limit_voter)
		return;
	fg->flip_charge_curr_voter = mca_find_votable("flip_charge_curr");
	if (fg->cfg.support_base_flip && !fg->flip_charge_curr_voter)
		return;
	fg->buck_input_voter = mca_find_votable("buck_input");
	if (!fg->buck_input_voter)
		return;

	fg->voter_ok = true;
}

static int strategy_fg_panel_notifier_cb(struct notifier_block *nb,
					 unsigned long event, void *val)
{
	struct strategy_fg *fg = container_of(nb, struct strategy_fg, panel_nb);
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	int status;

	switch (event) {
	case MCA_EVENT_PANEL_SCREEN_STATE_CHANGE:
		status = *(int *)val;
		mca_log_info("update screen_state: %d => %d\n",
			     fg->screen_status, status);
		fg->screen_status = !!status;
		break;
	default:
		break;
	}
	return 0;
}

#define NTC_SCALE_BOARDTEMP 100
static int strategy_fg_thermal_notifier_cb(struct notifier_block *nb,
					   unsigned long event, void *val)
{
	struct strategy_fg *fg =
		container_of(nb, struct strategy_fg, thermal_board_nb);
	if (fg && READ_ONCE(fg->stopping))
		return -ESHUTDOWN;

	switch (event) {
	case MCA_EVENT_THERMAL_BOARD_TEMP_CHANGE:
		fg->thermal_board_temp = *(int *)val / NTC_SCALE_BOARDTEMP;
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static int strategy_fg_probe(struct platform_device *pdev)
{
	struct strategy_fg *fg;
	int ret = 0;
	static int probe_cnt;

	mca_log_info("%s probe_begin, probe_cnt = %d\n", __func__, ++probe_cnt);
	fg = devm_kzalloc(&pdev->dev, sizeof(*fg), GFP_KERNEL);
	if (!fg) {
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}
	fg->dev = &pdev->dev;
	platform_set_drvdata(pdev, fg);
	if (devm_add_action_or_reset(&pdev->dev, mca_release_votables, fg))
		return -ENOMEM;
	ret = strategy_fg_parse_dt(fg);
	if (ret) {
		ret = -EPROBE_DEFER;
		mca_log_err("fg device found: %d\n", ret);
		return ret;
	}

	fg->batt_psy = power_supply_get_by_name("battery");
	charger_partition_get_eu_model(&fg->is_eu_model);
	if (fg->is_eu_model)
		fg->boot_update_soc = true;
	mca_log_info("probe is_eu_model[%d]\n", fg->is_eu_model);
	INIT_DELAYED_WORK(&fg->monitor_work, strategy_fg_monitor_workfunc);
	ret = strategy_class_fg_ops_register(fg, &g_strategy_fg_ops);
	strategy_fg_auth_create_group(fg->dev);
	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_FG,
					strategy_fg_process_event,
					strategy_fg_get_status, NULL, fg);
	INIT_DELAYED_WORK(&fg->delay_reset_full_flag_work,
			  delay_reset_full_flag_work);
	INIT_DELAYED_WORK(&fg->reset_default_work,
			  strategy_fg_reset_default_work);
	INIT_DELAYED_WORK(&fg->batt_abnormal_dfx_work,
			  strategy_batt_abnormal_dfx_work);
	queue_delayed_work(system_wq, &fg->batt_abnormal_dfx_work,
			   msecs_to_jiffies(STRATEGY_FG_ABNORMAL_DFX_DELAY));
	strategy_fg_init_voter(fg);
	INIT_DELAYED_WORK(&fg->dtpt_monitor_work,
			  strategy_fg_dtpt_monitor_work);
	INIT_DELAYED_WORK(&fg->fl4p0_calibration_work,
			  strategy_fl4p0_calibration_work);
	INIT_DELAYED_WORK(&fg->force_report_full_work,
			  strategy_force_report_full_work);
	INIT_DELAYED_WORK(&fg->ota_update_work,
			  strategy_nvt1000_ota_monitor_work);
	fg->fake_bap_match = STRATEGY_FG_FAKE_BAP_MATCH_NONE;
	fg->fake_soc = STRATEGY_FG_FAKE_SOC_NONE;
	fg->fake_temp = STRATEGY_FG_FAKE_TEMP_NONE;
	fg->update_period = STRATEGY_FG_UPDATE_PERIOD_NONE;
	fg->batt_health = POWER_SUPPLY_HEALTH_GOOD;
	fg->panel_nb.notifier_call = strategy_fg_panel_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_PANEL, &fg->panel_nb);
	fg->thermal_board_nb.notifier_call = strategy_fg_thermal_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_THERMAL_TEMP,
					&fg->thermal_board_nb);
	fg->reboot_nb.notifier_call = mca_strategy_fg_shutdown_cb;
	register_reboot_notifier(&fg->reboot_nb);

	if (fg->cfg.support_nvt1000_ota)
		queue_delayed_work(
			system_wq, &fg->ota_update_work,
			msecs_to_jiffies(NVT1000_OTA_MONITOR_INTERVAL));

	mca_queue_delayed_work(&fg->monitor_work, 0);
	mca_log_info("%s probe end\n", __func__);
	return ret;
}

static int strategy_fg_remove(struct platform_device *pdev)
{
	struct strategy_fg *fg = platform_get_drvdata(pdev);

	if (!fg)
		return 0;
	WRITE_ONCE(fg->stopping, true);
	mca_strategy_ops_unregister(STRATEGY_FUNC_TYPE_FG, fg);

	strategy_class_fg_ops_unregister(fg);
	strategy_fg_auth_remove_group(&pdev->dev);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_PANEL, &fg->panel_nb);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_THERMAL_TEMP, &fg->thermal_board_nb);
	unregister_reboot_notifier(&fg->reboot_nb);

	mca_cancel_delayed_work_sync(&fg->monitor_work);
	mca_cancel_delayed_work_sync(&fg->delay_reset_full_flag_work);
	mca_cancel_delayed_work_sync(&fg->reset_default_work);
	mca_cancel_delayed_work_sync(&fg->batt_abnormal_dfx_work);
	mca_cancel_delayed_work_sync(&fg->dtpt_monitor_work);
	mca_cancel_delayed_work_sync(&fg->fl4p0_calibration_work);
	mca_cancel_delayed_work_sync(&fg->force_report_full_work);
	mca_cancel_delayed_work_sync(&fg->ota_update_work);
	/* Drain work queued by a worker that was running during the first pass. */
	mca_cancel_delayed_work_sync(&fg->monitor_work);
	mca_cancel_delayed_work_sync(&fg->delay_reset_full_flag_work);
	mca_cancel_delayed_work_sync(&fg->reset_default_work);
	mca_cancel_delayed_work_sync(&fg->batt_abnormal_dfx_work);
	mca_cancel_delayed_work_sync(&fg->dtpt_monitor_work);
	mca_cancel_delayed_work_sync(&fg->fl4p0_calibration_work);
	mca_cancel_delayed_work_sync(&fg->force_report_full_work);
	mca_cancel_delayed_work_sync(&fg->ota_update_work);
	if (fg->batt_psy)
		power_supply_put(fg->batt_psy);

	mca_release_votables(fg);
	return 0;
}

static int strategy_fg_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct strategy_fg *fg =
		(struct strategy_fg *)platform_get_drvdata(pdev);
	ktime_t time_now = ktime_get_boottime();

	if (!fg)
		return -1;

	if (ktime_ms_delta(time_now, fg->suspend_time) >
	    STRATEGY_FG_RESUME_UPDATE_TIME_MS)
		fg->suspend_time = time_now;

	mca_cancel_delayed_work_sync(&fg->monitor_work);

	return 0;
}

static int strategy_fg_resume(struct platform_device *pdev)
{
	struct strategy_fg *fg =
		(struct strategy_fg *)platform_get_drvdata(pdev);
	int interval = STRATEGY_FG_WORK_INTERVAL_SCREEN_ON;

	if (!fg)
		return -1;

	if (ktime_ms_delta(ktime_get_boottime(), fg->suspend_time) >
	    STRATEGY_FG_RESUME_UPDATE_TIME_MS) {
		interval = 0;
		fg->resume_update_soc = true;
	} else {
		fg->resume_update_soc = false;
	}

	mca_queue_delayed_work(&fg->monitor_work, msecs_to_jiffies(interval));
	return 0;
}

static void strategy_fg_shutdown(struct platform_device *pdev)
{
	struct strategy_fg *fg =
		(struct strategy_fg *)platform_get_drvdata(pdev);
	if (fg && READ_ONCE(fg->stopping))
		return;

	mca_log_err(
		"battery shutdown, soc: %d, rsoc: %d, vbat: %d, vbat_mean: %d, empty: %d\n",
		fg->batt_ui_soc, fg->batt_rsoc, fg->batt_voltage,
		fg->batt_voltage_mean, fg->vbatt_empty);
}

static const struct of_device_id match_table[] = {
	{ .compatible = "xiaomi,strategy_fg" },
	{},
};

static struct platform_driver strategy_fg_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "strategy_fg",
		.of_match_table = match_table,
	},
	.probe = strategy_fg_probe,
	.remove = strategy_fg_remove,
	.suspend = strategy_fg_suspend,
	.resume = strategy_fg_resume,
	.shutdown = strategy_fg_shutdown,
};

static int __init strategy_fg_init(void)
{
	return platform_driver_register(&strategy_fg_driver);
}
module_init(strategy_fg_init);

static void __exit strategy_fg_exit(void)
{
	platform_driver_unregister(&strategy_fg_driver);
}
module_exit(strategy_fg_exit);

MODULE_DESCRIPTION("Strategy Fuel Gauge");
MODULE_AUTHOR("liweiwei9@xiaomi.com");
MODULE_LICENSE("GPL v2");

MODULE_DEVICE_TABLE(of, match_table);
