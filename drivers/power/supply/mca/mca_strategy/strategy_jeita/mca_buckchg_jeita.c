// SPDX-License-Identifier: GPL-2.0
/*
 * Single-pack JEITA control adapted from Xiaomi's public Onyx MCA source.
 * Copyright (c) 2024 Xiaomi Technologies Co., Ltd.
 *
 * Use the device's DT tables, not Onyx battery limits. BAA changes termination
 * parameters by DT row index; it does not supply a replacement charge_current policy.
 */
#include <mca/common/mca_workqueue.h>

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <hwid.h>
#include "mca_jeita_policy.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_buckchg_jeita"
#endif

#define JEITA_MONITOR_MS 3000
#define JEITA_RETRY_MS 1000
#define JEITA_DT_COLUMNS 8
#define JEITA_ABNORMAL_FV_MAX 4200
#define JEITA_HOT_RECHARGE_HYST 150

struct dada_jeita_tables {
	struct mca_jeita_table normal;
	struct mca_jeita_table ffc;
};

struct dada_jeita_data {
	struct device *dev;
	struct mutex lock;
	struct delayed_work monitor_work;
	struct notifier_block connect_nb;
	struct mca_smartchg_if_ops smart_ops;
	struct dada_jeita_tables tables;
	int previous_band;
	int previous_current;
	int previous_ffc;
	int low_hyst;
	int cold_low_hyst;
	int hot_term_hyst;
	int delta_fv;
	bool wired_online;
	bool wireless_online;
	bool base_flip_same;
	bool dtpt;
	bool hot_stop;
	bool policy_ready;
	bool votes_active;
	bool stopping;
};

static int dada_jeita_read_int(struct device_node *node, const char *name,
			       int index, int *value)
{
	const char *str;
	int ret = of_property_read_string_index(node, name, index, &str);

	return ret ? ret : kstrtoint(str, 10, value);
}

static int dada_jeita_parse_voltages(struct device_node *node, const char *name,
				   struct mca_jeita_band *band)
{
	int count, i, ret;

	if (!strcmp(name, "null"))
		return 0;
	count = of_property_count_strings(node, name);
	if (count <= 0 || count % 2 || count > MCA_JEITA_MAX_VOLTAGES * 2)
		return -EINVAL;
	band->voltage_count = count / 2;
	for (i = 0; i < band->voltage_count; i++) {
		ret = dada_jeita_read_int(node, name, i * 2,
					 &band->voltages[i].voltage);
		if (ret)
			return ret;
		ret = dada_jeita_read_int(node, name, i * 2 + 1,
					 &band->voltages[i].charge_current);
		if (ret)
			return ret;
	}
	return 0;
}

static int dada_jeita_parse_table(struct device_node *node, const char *name,
				 struct mca_jeita_table *table)
{
	int count, row, col, ret;

	count = of_property_count_strings(node, name);
	if (count <= 0 || count % JEITA_DT_COLUMNS ||
	    count > MCA_JEITA_MAX_BANDS * JEITA_DT_COLUMNS)
		return -EINVAL;
	table->count = count / JEITA_DT_COLUMNS;
	for (row = 0; row < table->count; row++) {
		struct mca_jeita_band *band = &table->bands[row];
		int *fields[] = { &band->temp_low, &band->temp_high,
			&band->low_hyst, &band->high_hyst, &band->charge_current,
			&band->vterm, &band->iterm };
		const char *voltage_name;

		for (col = 0; col < ARRAY_SIZE(fields); col++) {
			ret = dada_jeita_read_int(node, name,
				row * JEITA_DT_COLUMNS + col, fields[col]);
			if (ret)
				return ret;
		}
		ret = of_property_read_string_index(node, name,
			row * JEITA_DT_COLUMNS + 7, &voltage_name);
		if (ret)
			return ret;
		ret = dada_jeita_parse_voltages(node, voltage_name, band);
		if (ret)
			return ret;
	}
	return mca_jeita_table_valid(table) ? 0 : -EINVAL;
}

static int dada_jeita_read_hyst(struct device_node *node, const char *name,
				int fallback, int *value)
{
	u32 val;

	if (of_property_read_u32(node, name, &val))
		val = fallback;
	if (val > 1000)
		return -EINVAL;
	*value = val;
	return 0;
}

static int dada_jeita_parse_dt(struct dada_jeita_data *info)
{
	struct device_node *node = of_node_get(info->dev->of_node);
	const struct mca_hwid *hwid = mca_get_hwid_info();
	const char *name = NULL;
	int ret;

	ret = dada_jeita_read_hyst(node, "vbat_low_hyst", 50, &info->low_hyst);
	if (!ret)
		ret = dada_jeita_read_hyst(node, "vbat_low_cold_hyst", 50,
					  &info->cold_low_hyst);
	if (!ret)
		ret = dada_jeita_read_hyst(node, "jeita_hot_termination_hyst", 0,
					  &info->hot_term_hyst);
	if (ret)
		goto out;

	if (of_property_read_bool(node, "has-global-batt-para")) {
		if (!hwid) {
			ret = -EPROBE_DEFER;
			goto out;
		}
		if (hwid->country_version != CountryCN) {
			of_node_put(node);
			node = of_find_node_by_name(NULL, "mca_buckchg_jeita_gbl_para");
			if (!node) {
				ret = -EINVAL;
				goto out;
			}
		}
	}
	if (of_property_read_bool(node, "has-tmp-batt-para")) {
		ret = platform_fg_ops_get_device_name(FG_IC_MASTER, &name);
		if (ret || !name) {
			ret = -EPROBE_DEFER;
			goto out;
		}
		if (!strcmp(name, "2@BP")) {
			of_node_put(node);
			node = of_find_node_by_name(NULL, "mca_buckchg_jeita_tmp_para");
			if (!node) {
				ret = -EINVAL;
				goto out;
			}
		}
	}
	/* Foldable base/flip policy needs separate gauges/load-switch control. */
	if (of_property_read_bool(node, "support-base-flip")) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	info->base_flip_same = of_property_read_bool(node, "base-flip-same");
	ret = dada_jeita_parse_table(node, "jeita_para", &info->tables.normal);
	if (!ret && of_find_property(node, "jeita_para_ffc", NULL))
		ret = dada_jeita_parse_table(node, "jeita_para_ffc", &info->tables.ffc);
out:
	of_node_put(node);
	return ret;
}

static int dada_jeita_vote(const char *name, const char *client,
			    bool enabled, int value)
{
	/* Replay unchanged limits after GLINK recovery or a failed write. */
	return mca_buckchg_policy_vote(name, client, enabled, value);
}

static void dada_jeita_clear_votes(void)
{
	dada_jeita_vote("chg_enable", "jeita", false, 1);
	dada_jeita_vote("chg_enable", "jeita-hot", false, 1);
	dada_jeita_vote("buck_charge_curr", "jeita", false, 0);
	dada_jeita_vote("term_volt", "jeita", false, 0);
	dada_jeita_vote("term_curr", "jeita", false, 0);
}

static int dada_jeita_update(struct dada_jeita_data *info)
{
	const struct mca_jeita_table *table;
	const struct mca_jeita_band *band;
	int temp, vbat, ffc, index, charge_current, table_current, vterm, low_hyst, ret;

	info->votes_active = true;
	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (!ret)
		ret = strategy_class_fg_ops_get_voltage(&vbat);
	ffc = strategy_class_fg_get_fastcharge();
	if (ret || ffc < 0) {
		info->policy_ready = false;
		dada_jeita_vote("chg_enable", "jeita", true, 0);
		return ret ? ret : ffc;
	}
	ffc = !!ffc && info->tables.ffc.count;
	table = ffc ? &info->tables.ffc : &info->tables.normal;
	if (ffc != info->previous_ffc) {
		info->previous_band = -1;
		info->previous_current = -1;
	}
	index = mca_jeita_select_band(table, temp, info->previous_band,
				     mca_smartchg_is_extreme_cold_enabled());
	if (index < 0) {
		info->policy_ready = false;
		info->previous_band = -1;
		info->previous_current = -1;
		dada_jeita_vote("chg_enable", "jeita", true, 0);
		return -ERANGE;
	}
	band = &table->bands[index];
	low_hyst = index == 1 || index == 2 ? info->cold_low_hyst : info->low_hyst;
	charge_current = mca_jeita_select_current(band, vbat,
		index == info->previous_band ? info->previous_current : -1, low_hyst);
	table_current = charge_current;
	vterm = band->vterm;
	if (vterm > JEITA_ABNORMAL_FV_MAX) {
		if (info->delta_fv >= vterm) {
			info->policy_ready = false;
			dada_jeita_vote("chg_enable", "jeita", true, 0);
			return -ERANGE;
		}
		vterm -= info->delta_fv;
		info->hot_stop = false;
	} else if (vbat >= vterm - info->hot_term_hyst) {
		info->hot_stop = true;
	} else if (vbat < vterm - JEITA_HOT_RECHARGE_HYST) {
		info->hot_stop = false;
	}
	/* Reduce before dividing without risking a large-table integer overflow. */
	if (info->dtpt)
		charge_current = (long long)charge_current * 8 / 10;

	/* Hold charging on initial configuration, without toggling it each poll. */
	ret = info->policy_ready && charge_current > 0 && !info->hot_stop ? 0 :
		dada_jeita_vote("chg_enable", "jeita", true, 0);
	if (!ret)
		ret = dada_jeita_vote("buck_charge_curr", "jeita", true, charge_current);
	if (!ret)
		ret = dada_jeita_vote("term_volt", "jeita", true, vterm);
	if (!ret)
		ret = dada_jeita_vote("term_curr", "jeita", true, band->iterm);
	if (!ret)
		ret = dada_jeita_vote("chg_enable", "jeita-hot", true, !info->hot_stop);
	if (!ret)
		ret = dada_jeita_vote("chg_enable", "jeita", true, charge_current > 0);
	if (ret) {
		info->policy_ready = false;
		dada_jeita_vote("chg_enable", "jeita", true, 0);
		return ret;
	}
	info->policy_ready = true;
	info->previous_band = index;
	/* Hysteresis tracks the table charge_current, not the separate DTPT derating. */
	info->previous_current = table_current;
	info->previous_ffc = ffc;
	return 0;
}

static void dada_jeita_monitor(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct dada_jeita_data *info = container_of(to_delayed_work(work),
						 struct dada_jeita_data, monitor_work);
	int wired, wireless, ret = 0;

	mutex_lock(&info->lock);
	if (info->stopping)
		goto out;
	/* Also discover sources that were already online when this module loaded. */
	if (!platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &wired))
		info->wired_online = !!wired;
	if (!platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &wireless))
		info->wireless_online = !!wireless;
	if (info->wired_online || info->wireless_online)
		ret = dada_jeita_update(info);
	else {
		if (info->votes_active)
			dada_jeita_clear_votes();
		info->votes_active = false;
		info->policy_ready = false;
		info->previous_band = -1;
		info->previous_current = -1;
		info->hot_stop = false;
	}
	if (ret)
		dev_warn_ratelimited(info->dev, "JEITA policy not applied: %d\n", ret);
	/* Serialize requeue with stop/update so teardown cannot resurrect work. */
	schedule_delayed_work(&info->monitor_work,
		msecs_to_jiffies(ret ? JEITA_RETRY_MS : JEITA_MONITOR_MS));
out:
	mutex_unlock(&info->lock);
}

static int dada_jeita_process_event(int event, int value, void *data)
{
	struct dada_jeita_data *info = data;

	mutex_lock(&info->lock);
	if (info->stopping) {
		mutex_unlock(&info->lock);
		return -ENODEV;
	}
	switch (event) {
	case MCA_EVENT_USB_CONNECT:
		info->wired_online = true;
		break;
	case MCA_EVENT_USB_DISCONNECT:
		info->wired_online = false;
		break;
	case MCA_EVENT_WIRELESS_CONNECT:
		info->wireless_online = true;
		break;
	case MCA_EVENT_WIRELESS_DISCONNECT:
		info->wireless_online = false;
		break;
	case MCA_EVENT_BATTERY_DTPT:
		info->dtpt = !!value;
		break;
	default:
		mutex_unlock(&info->lock);
		return -EOPNOTSUPP;
	}
	info->previous_band = -1;
	info->previous_current = -1;
	info->policy_ready = false;
	mod_delayed_work(system_wq, &info->monitor_work, 0);
	mutex_unlock(&info->lock);
	return 0;
}

static int dada_jeita_connect_event(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	struct dada_jeita_data *info = container_of(nb, struct dada_jeita_data,
						   connect_nb);

	return dada_jeita_process_event(event, 0, info) ? NOTIFY_DONE : NOTIFY_OK;
}

static int dada_jeita_get_status(int status, void *value, void *data)
{
	struct dada_jeita_data *info = data;
	const struct mca_jeita_table *table;
	int temp, index, ret;

	if (!value)
		return -EINVAL;
	if (status != STRATEGY_STATUS_TYPE_JEITA_COLD_ZONE &&
	    status != STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM &&
	    status != STRATEGY_STATUS_TYPE_JEITA_NORMAL_VTERM &&
	    status != STRATEGY_STATUS_TYPE_JEITA_FFC_VTERM)
		return -EOPNOTSUPP;
	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret)
		return ret;
	mutex_lock(&info->lock);
	if (status == STRATEGY_STATUS_TYPE_JEITA_COLD_ZONE) {
		*(int *)value = info->base_flip_same && !info->previous_ffc &&
				info->previous_band == 1;
		ret = 0;
		goto out;
	}
	table = status == STRATEGY_STATUS_TYPE_JEITA_NORMAL_VTERM ?
		&info->tables.normal : &info->tables.ffc;
	index = mca_jeita_find_band(table, temp);
	if (index < 0) {
		ret = table->count ? -ERANGE : -ENODATA;
		goto out;
	}
	switch (status) {
	case STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM:
		*(int *)value = table->bands[index].iterm;
		break;
	case STRATEGY_STATUS_TYPE_JEITA_NORMAL_VTERM:
	case STRATEGY_STATUS_TYPE_JEITA_FFC_VTERM:
		*(int *)value = table->bands[index].vterm;
		break;
	default:
		ret = -EOPNOTSUPP;
	}
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int dada_jeita_apply_baa(struct mca_jeita_table *table,
	const struct smart_batt_jeita_term_para *records, int count)
{
	unsigned int seen = 0;
	int i;

	if (count < 0 || count > table->count)
		return -EINVAL;
	for (i = 0; i < count; i++) {
		int index = records[i].t_range.idx;

		if (index < 0 || index >= table->count || (seen & BIT(index)) ||
		    records[i].t_range.min >= records[i].t_range.max ||
		    records[i].vterm <= 0 || records[i].iterm < 0)
			return -EINVAL;
		seen |= BIT(index);
		if (!mca_jeita_update_term(table, index, records[i].vterm,
					   records[i].iterm))
			return -EINVAL;
	}
	return mca_jeita_table_valid(table) ? 0 : -EINVAL;
}

static int dada_jeita_update_baa(void *data, char *payload,
				int ffc_count, int normal_count)
{
	struct dada_jeita_data *info = data;
	const struct smart_batt_jeita_term_para *records = (void *)payload;
	struct dada_jeita_tables *copy;
	int ret;

	if (!payload || ffc_count < 0 || normal_count < 0 ||
	    ffc_count > MCA_JEITA_MAX_BANDS || normal_count > MCA_JEITA_MAX_BANDS)
		return -EINVAL;
	mutex_lock(&info->lock);
	copy = kmemdup(&info->tables, sizeof(*copy), GFP_KERNEL);
	if (!copy) {
		ret = -ENOMEM;
		goto out;
	}
	ret = dada_jeita_apply_baa(&copy->ffc, records, ffc_count);
	if (!ret)
		ret = dada_jeita_apply_baa(&copy->normal, records + ffc_count,
					  normal_count);
	if (!ret) {
		info->tables = *copy;
		info->previous_band = -1;
		info->previous_current = -1;
		if (!info->stopping)
			mod_delayed_work(system_wq, &info->monitor_work, 0);
	}
	kfree(copy);
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int dada_jeita_set_delta_fv(void *data, int value)
{
	struct dada_jeita_data *info = data;

	if (value < 0)
		return -EINVAL;
	mutex_lock(&info->lock);
	info->delta_fv = value;
	if (!info->stopping)
		mod_delayed_work(system_wq, &info->monitor_work, 0);
	mutex_unlock(&info->lock);
	return 0;
}

static void dada_jeita_stop(struct dada_jeita_data *info)
{
	mutex_lock(&info->lock);
	info->stopping = true;
	mutex_unlock(&info->lock);
	mca_cancel_delayed_work_sync(&info->monitor_work);
}

static int dada_jeita_probe(struct platform_device *pdev)
{
	struct dada_jeita_data *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	info->previous_band = -1;
	info->previous_current = -1;
	info->previous_ffc = -1;
	mutex_init(&info->lock);
	INIT_DELAYED_WORK(&info->monitor_work, dada_jeita_monitor);
	ret = dada_jeita_parse_dt(info);
	if (ret)
		return dev_err_probe(info->dev, ret, "invalid JEITA DT policy\n");
	platform_set_drvdata(pdev, info);
	ret = mca_strategy_ops_register(STRATEGY_FUNC_TYPE_JEITA,
		dada_jeita_process_event, dada_jeita_get_status, NULL, info);
	if (ret)
		return ret;
	info->smart_ops.type = MCA_SMARTCHG_IF_CHG_TYPE_JEITA;
	info->smart_ops.data = info;
	info->smart_ops.set_delta_fv = dada_jeita_set_delta_fv;
	info->smart_ops.update_baa_para = dada_jeita_update_baa;
	ret = mca_smartchg_if_ops_register(&info->smart_ops);
	if (ret)
		goto err_strategy;
	info->connect_nb.notifier_call = dada_jeita_connect_event;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					      &info->connect_nb);
	if (ret)
		goto err_smart;
	schedule_delayed_work(&info->monitor_work, 0);
	dev_info(info->dev, "JEITA DT policy registered (%d normal, %d FFC rows)\n",
		 info->tables.normal.count, info->tables.ffc.count);
	return 0;

err_smart:
	dada_jeita_stop(info);
	mca_smartchg_if_ops_unregister(&info->smart_ops);
err_strategy:
	dada_jeita_stop(info);
	mca_strategy_ops_unregister(STRATEGY_FUNC_TYPE_JEITA, info);
	if (info->votes_active)
		dada_jeita_clear_votes();
	return ret;
}

static int dada_jeita_remove(struct platform_device *pdev)
{
	struct dada_jeita_data *info = platform_get_drvdata(pdev);

	dada_jeita_stop(info);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &info->connect_nb);
	mca_smartchg_if_ops_unregister(&info->smart_ops);
	mca_strategy_ops_unregister(STRATEGY_FUNC_TYPE_JEITA, info);
	if (info->votes_active)
		dada_jeita_clear_votes();
	return 0;
}

static void dada_jeita_shutdown(struct platform_device *pdev)
{
	dada_jeita_stop(platform_get_drvdata(pdev));
}

static const struct of_device_id dada_jeita_match[] = {
	{ .compatible = "mca,buckchg_jeita" },
	{},
};
MODULE_DEVICE_TABLE(of, dada_jeita_match);

static struct platform_driver dada_jeita_driver = {
	.driver = {
		.name = "mca_buckchg_jeita",
		.of_match_table = dada_jeita_match,
	},
	.probe = dada_jeita_probe,
	.remove = dada_jeita_remove,
	.shutdown = dada_jeita_shutdown,
};
module_platform_driver(dada_jeita_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA single-pack JEITA charging policy");
MODULE_LICENSE("GPL v2");
