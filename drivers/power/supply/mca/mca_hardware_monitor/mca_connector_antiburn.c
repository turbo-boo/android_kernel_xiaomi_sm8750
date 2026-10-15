// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi Dada MCA connector anti-burn protection.
 *
 * Dada enables the newer elaboration strategy.  In that mode connector NTC
 * values and the DT trigger/rate thresholds are expressed in milli-degrees C;
 * the legacy path keeps Xiaomi's older degree-C semantics.  The distinction is
 * verified against Dada's stock mca_connector_antiburn.ko.
 */
#include <mca/common/mca_workqueue.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_connector_antiburn"
#endif

#define CONNECTOR_NTC_NUM 2
#define ANTIBURN_INTERVAL_FAST_MS 1000
#define ANTIBURN_INTERVAL_IDLE_MS 5000
#define ANTIBURN_VBUS_SAFE_UV 6000000
#define ANTIBURN_VBUS_RESET_UV 3600000
#define ANTIBURN_VBUS_HW_ERR_UV 4100000
#define ANTIBURN_RESET_RETRIES 5
#define ANTIBURN_RESET_DELAY_MS 200

enum connector_sysfs_attr {
	CONNECTOR_TEMP_1 = 0,
	CONNECTOR_TEMP_2,
	CONNECTOR_RESET_VSAFE0V,
	CONNECTOR_NTC_ALARM,
	CONNECTOR_MOS_CTRL,
};

struct connector_antiburn {
	struct device *dev;
	struct thermal_zone_device *tzd[CONNECTOR_NTC_NUM];
	struct notifier_block thermal_nb;
	struct notifier_block connect_nb;
	struct notifier_block debug_nb;
	struct delayed_work monitor_work;
	ktime_t last_sample;
	int last_temp[CONNECTOR_NTC_NUM];
	int temperature[CONNECTOR_NTC_NUM];
	int temp_rate[CONNECTOR_NTC_NUM];
	int fake_temp[CONNECTOR_NTC_NUM];
	int thermal_board_temp;
	int trigger_temp;
	int recharge_temp;
	int combined_board_trigger_temp;
	int combined_rate_trigger_temp;
	int max_thermal_board_temp;
	int max_temp_increase_rate;
	int monitor_interval;
	int mos_ctrl_gpio;
	int support_soft;
	int support_hw;
	int use_double_ntc;
	int otg_detect_en;
	int otg_boost_src;
	int en_src;
	int support_elaboration;
	bool support_base_flip;
	bool triggered;
	bool reset_vsafe0v;
	bool ntc_alarm;
	bool disable_antiburn;
	bool first_sample;
	const char *thermal_zone_name[CONNECTOR_NTC_NUM];
};

static struct connector_antiburn *g_conn;

static int antiburn_default_temp(const struct connector_antiburn *conn)
{
	return conn->support_elaboration ? 25000 : 25;
}

static int antiburn_read_temp(struct connector_antiburn *conn, int index)
{
	int temp, ret;

	if (index < 0 || index >= CONNECTOR_NTC_NUM)
		return antiburn_default_temp(conn);
	if (conn->fake_temp[index])
		return conn->fake_temp[index];
	if (!conn->tzd[index] || IS_ERR(conn->tzd[index]))
		return conn->temperature[index] ?: antiburn_default_temp(conn);

	temp = conn->support_elaboration ? 25000 : 25;
	ret = thermal_zone_get_temp(conn->tzd[index], &temp);
	if (ret)
		return conn->temperature[index] ?: antiburn_default_temp(conn);
	return conn->support_elaboration ? temp : temp / 1000;
}

static int antiburn_dump_log_head(void *data, char *buf, int size)
{
	return scnprintf(buf, size, "port_temp port_temp1 shell_temp ");
}

static int antiburn_dump_log_context(void *data, char *buf, int size)
{
	struct connector_antiburn *conn = data;
	int temp1, temp2 = -1;

	if (!conn)
		return scnprintf(buf, size, "%-10d%-11d%-11d", -1, -1, -1);
	temp1 = antiburn_read_temp(conn, CONNECTOR_TEMP_1);
	if (conn->use_double_ntc)
		temp2 = antiburn_read_temp(conn, CONNECTOR_TEMP_2);
	return scnprintf(buf, size, "%-10d%-11d%-11d", temp1, temp2,
			 conn->thermal_board_temp);
}

static struct mca_log_charge_log_ops antiburn_log_ops = {
	.dump_log_head = antiburn_dump_log_head,
	.dump_log_context = antiburn_dump_log_context,
};

static void antiburn_update_rates(struct connector_antiburn *conn)
{
	ktime_t now = ktime_get();
	s64 gap_ms;
	int i;

	if (!conn->first_sample) {
		conn->last_sample = now;
		for (i = 0; i < CONNECTOR_NTC_NUM; i++) {
			conn->last_temp[i] = conn->temperature[i];
			conn->temp_rate[i] = 0;
		}
		conn->first_sample = true;
		return;
	}

	gap_ms = ktime_to_ms(ktime_sub(now, conn->last_sample));
	if (gap_ms < 1)
		return;
	for (i = 0; i < CONNECTOR_NTC_NUM; i++) {
		conn->temp_rate[i] =
			(conn->temperature[i] - conn->last_temp[i]) * 1000 / gap_ms;
		conn->last_temp[i] = conn->temperature[i];
	}
	conn->last_sample = now;
}

static void antiburn_uevent(const char *name, int value)
{
	char event[MCA_EVENT_NOTIFY_SIZE];
	struct mca_event_notify_data data;
	int len;

	len = scnprintf(event, sizeof(event), "%s=%d", name, value);
	data.event = event;
	data.event_len = len;
	mca_event_report_uevent(&data);
}

static unsigned int antiburn_real_type(void)
{
	unsigned int type = XM_CHARGER_TYPE_UNKNOW;

	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PPS, &type) &&
	    type != XM_CHARGER_TYPE_UNKNOW)
		return type;
	type = XM_CHARGER_TYPE_UNKNOW;
	if (!protocol_class_get_adapter_type(ADAPTER_PROTOCOL_PD, &type) &&
	    type != XM_CHARGER_TYPE_UNKNOW)
		return type;
	type = XM_CHARGER_TYPE_UNKNOW;
	(void)protocol_class_get_adapter_type(ADAPTER_PROTOCOL_QC, &type);
	return type;
}

static void antiburn_reset_pd_vsafe0v(struct connector_antiburn *conn)
{
	unsigned int dummy = 0;
	int vbus = 0, i;

	for (i = 0; i < ANTIBURN_RESET_RETRIES; i++) {
		(void)protocol_class_pd_request_vdm_cmd(TYPEC_PORT_0,
			USBPD_UVDM_RESET_VSAFE0V, &dummy, 0);
		msleep(ANTIBURN_RESET_DELAY_MS);
		if (platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus))
			continue;
		if (vbus < ANTIBURN_VBUS_SAFE_UV) {
			if (vbus < ANTIBURN_VBUS_RESET_UV) {
				conn->reset_vsafe0v = true;
				antiburn_uevent("POWER_SUPPLY_ADAPTER_RESET_VSAFE0V", 1);
			}
			break;
		}
	}
}

static void antiburn_set_mos(struct connector_antiburn *conn, bool open)
{
	if (!conn->support_hw || !gpio_is_valid(conn->mos_ctrl_gpio))
		return;
	gpio_direction_output(conn->mos_ctrl_gpio, open ? 1 : 0);
}

static void antiburn_trigger(struct connector_antiburn *conn, int connector_temp)
{
	unsigned int type = antiburn_real_type();
	bool otg = false;
	int vbus = 0;

	conn->triggered = true;
	conn->ntc_alarm = true;
	antiburn_uevent("POWER_SUPPLY_CONNECTOR_TEMP", connector_temp);
	antiburn_uevent("POWER_SUPPLY_NTC_ALARM", 1);

	if (type == XM_CHARGER_TYPE_PD || type == XM_CHARGER_TYPE_PD_VERIFY ||
	    type == XM_CHARGER_TYPE_PPS)
		antiburn_reset_pd_vsafe0v(conn);

	mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
			       MCA_EVENT_CONN_ANTIBURN_CHANGE, NULL);
	mca_charge_mievent_report(CHARGE_DFX_ANTI_BURN_TRIGGERED,
				  &connector_temp, 1);

	if (conn->otg_detect_en)
		(void)protocol_class_pd_get_otg_plugin_status(TYPEC_PORT_0, &otg);
	if (otg)
		(void)platform_class_buckchg_ops_set_boost_enable(
			MAIN_BUCK_CHARGER,
			(conn->en_src << 16) | (conn->otg_boost_src << 8));

	antiburn_set_mos(conn, true);
	if (conn->support_hw) {
		msleep(50);
		if (!platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER,
						       &vbus) &&
		    vbus >= ANTIBURN_VBUS_HW_ERR_UV)
			mca_charge_mievent_report(CHARGE_DFX_ANTIBURN_ERR, NULL, 0);
	}
}

static void antiburn_recover(struct connector_antiburn *conn, int connector_temp)
{
	conn->triggered = false;
	conn->reset_vsafe0v = false;
	antiburn_set_mos(conn, false);
	antiburn_uevent("POWER_SUPPLY_CONNECTOR_TEMP", connector_temp);
	antiburn_uevent("POWER_SUPPLY_ADAPTER_RESET_VSAFE0V", 0);
	mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
			       MCA_EVENT_CONN_ANTIBURN_CHANGE, NULL);
	mca_charge_mievent_set_state(MIEVENT_STATE_END,
				     CHARGE_DFX_ANTI_BURN_TRIGGERED);
}

static void antiburn_check_status(struct connector_antiburn *conn)
{
	int hot_idx, connector_temp, rate;
	int online = 0;
	bool cid = false, otg = false;
	bool trigger;

	hot_idx = conn->temperature[1] > conn->temperature[0] ? 1 : 0;
	connector_temp = conn->temperature[hot_idx];
	rate = conn->temp_rate[hot_idx];
	if (connector_temp < antiburn_default_temp(conn) +
		(conn->support_elaboration ? 1000 : 1))
		connector_temp = antiburn_default_temp(conn);

	(void)platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &online);
	(void)protocol_class_pd_get_cid_status(TYPEC_PORT_0, &cid);
	if (conn->otg_detect_en)
		(void)protocol_class_pd_get_otg_plugin_status(TYPEC_PORT_0, &otg);

	trigger = connector_temp >= conn->trigger_temp ||
		(connector_temp >= conn->combined_board_trigger_temp &&
		 conn->thermal_board_temp <= conn->max_thermal_board_temp) ||
		(connector_temp >= conn->combined_rate_trigger_temp &&
		 rate >= conn->max_temp_increase_rate);

	if (trigger && !conn->triggered && !conn->disable_antiburn &&
	    (cid || online))
		antiburn_trigger(conn, connector_temp);
	else if (conn->triggered && connector_temp < conn->recharge_temp &&
		 rate < conn->max_temp_increase_rate && !otg && !cid)
		antiburn_recover(conn, connector_temp);

	if (conn->ntc_alarm && !cid) {
		conn->ntc_alarm = false;
		antiburn_uevent("POWER_SUPPLY_CONNECTOR_TEMP", connector_temp);
		antiburn_uevent("POWER_SUPPLY_NTC_ALARM", 0);
	}

	conn->monitor_interval = (otg || online) ? ANTIBURN_INTERVAL_FAST_MS :
		ANTIBURN_INTERVAL_IDLE_MS;
}

static void antiburn_monitor_workfn(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct connector_antiburn *conn = container_of(
		work, struct connector_antiburn, monitor_work.work);
	int i;

	for (i = 0; i < CONNECTOR_NTC_NUM; i++)
		conn->temperature[i] = antiburn_read_temp(conn, i);
	antiburn_update_rates(conn);
	antiburn_check_status(conn);
	schedule_delayed_work(&conn->monitor_work,
			      msecs_to_jiffies(conn->monitor_interval));
}

int connector_antiburn_is_triggered(void)
{
	return g_conn ? g_conn->triggered : 0;
}
EXPORT_SYMBOL(connector_antiburn_is_triggered);

#ifdef CONFIG_SYSFS
static ssize_t antiburn_sysfs_show(struct device *dev,
				   struct device_attribute *attr, char *buf);
static ssize_t antiburn_sysfs_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count);

static struct mca_sysfs_attr_info antiburn_fields[] = {
	mca_sysfs_attr_rw(antiburn_sysfs, 0664, CONNECTOR_TEMP_1, connector_temp_1),
	mca_sysfs_attr_rw(antiburn_sysfs, 0664, CONNECTOR_TEMP_2, connector_temp_2),
	mca_sysfs_attr_rw(antiburn_sysfs, 0664, CONNECTOR_RESET_VSAFE0V, reset_vsafe0V),
	mca_sysfs_attr_rw(antiburn_sysfs, 0664, CONNECTOR_NTC_ALARM, ntc_alarm),
	mca_sysfs_attr_rw(antiburn_sysfs, 0664, CONNECTOR_MOS_CTRL, mos_ctrl),
};
#define ANTIBURN_ATTRS ARRAY_SIZE(antiburn_fields)
static struct attribute *antiburn_attrs[ANTIBURN_ATTRS + 1];
static const struct attribute_group antiburn_group = { .attrs = antiburn_attrs };

static struct connector_antiburn *antiburn_from_attr(struct device *dev,
						      struct device_attribute *attr,
						      struct mca_sysfs_attr_info **field)
{
	*field = mca_sysfs_lookup_attr(attr->attr.name, antiburn_fields,
				       ANTIBURN_ATTRS);
	return *field ? dev_get_drvdata(dev) : NULL;
}

static ssize_t antiburn_sysfs_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *field;
	struct connector_antiburn *conn = antiburn_from_attr(dev, attr, &field);
	int value;

	if (!conn)
		return -ENODEV;
	switch (field->sysfs_attr_name) {
	case CONNECTOR_TEMP_1:
	case CONNECTOR_TEMP_2:
		value = antiburn_read_temp(conn, field->sysfs_attr_name);
		if (!conn->support_elaboration)
			value *= 10;
		return sysfs_emit(buf, "%d\n", value);
	case CONNECTOR_RESET_VSAFE0V:
		return sysfs_emit(buf, "%d\n", conn->reset_vsafe0v);
	case CONNECTOR_NTC_ALARM:
		return sysfs_emit(buf, "%d\n", conn->ntc_alarm);
	case CONNECTOR_MOS_CTRL:
		if (!conn->support_hw || !gpio_is_valid(conn->mos_ctrl_gpio))
			return sysfs_emit(buf, "0\n");
		return sysfs_emit(buf, "%d\n", gpio_get_value(conn->mos_ctrl_gpio));
	default:
		return -EINVAL;
	}
}

static ssize_t antiburn_sysfs_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *field;
	struct connector_antiburn *conn = antiburn_from_attr(dev, attr, &field);
	int value;

	if (!conn || kstrtoint(buf, 10, &value))
		return -EINVAL;
	switch (field->sysfs_attr_name) {
	case CONNECTOR_TEMP_1:
	case CONNECTOR_TEMP_2:
		conn->fake_temp[field->sysfs_attr_name] = value / 10;
		cancel_delayed_work_sync(&conn->monitor_work);
		schedule_delayed_work(&conn->monitor_work, 0);
		break;
	case CONNECTOR_MOS_CTRL:
		if (conn->support_hw && gpio_is_valid(conn->mos_ctrl_gpio) &&
		    (value == 0 || value == 1))
			gpio_direction_output(conn->mos_ctrl_gpio, value);
		break;
	default:
		break;
	}
	return count;
}
#endif

static int antiburn_thermal_event(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct connector_antiburn *conn = container_of(nb, struct connector_antiburn,
							thermal_nb);

	if (event == MCA_EVENT_THERMAL_BOARD_TEMP_CHANGE && data)
		conn->thermal_board_temp = *(int *)data / 1000;
	return NOTIFY_DONE;
}

static int antiburn_connect_event(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct connector_antiburn *conn = container_of(nb, struct connector_antiburn,
							connect_nb);

	if (event == MCA_EVENT_USB_CONNECT) {
		cancel_delayed_work_sync(&conn->monitor_work);
		conn->monitor_interval = ANTIBURN_INTERVAL_FAST_MS;
		schedule_delayed_work(&conn->monitor_work, 0);
	}
	return NOTIFY_DONE;
}

static int antiburn_debug_event(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct connector_antiburn *conn = container_of(nb, struct connector_antiburn,
							debug_nb);

	if (!data)
		return NOTIFY_DONE;
	if (event == MCA_EVENT_DEBUG_CTRL_DOUBLE85 ||
	    event == MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT ||
	    event == MCA_EVENT_DEBUG_CTRL_MEMORY_TEST)
		conn->disable_antiburn = !!(*(int *)data);
	return NOTIFY_DONE;
}

static int antiburn_read_u32(struct device_node *np, const char *name,
			     int *dst, int def)
{
	u32 value;

	if (of_property_read_u32(np, name, &value))
		*dst = def;
	else
		*dst = value;
	return 0;
}

static int antiburn_probe(struct platform_device *pdev)
{
	struct connector_antiburn *conn;
	struct device_node *np = pdev->dev.of_node;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	int def_scale, ret;

	if (!hwid)
		return -ENOMEM;
	/*
	 * Exact stock Dada gate.  Stock reads hwid+0 (platform), hwid+16
	 * (build_version) and hwid+12 (minor_version): an early hardware
	 * revision without anti-burn.
	 */
	if (hwid->platform_version == 1 && hwid->build_version == 0 &&
	    hwid->minor_version == 1) {
		mca_log_info("anti-burn unsupported on %s P%u.%u\n",
			     hwid->product_name ?: "unknown", hwid->build_version,
			     hwid->minor_version);
		return 0;
	}

	conn = devm_kzalloc(&pdev->dev, sizeof(*conn), GFP_KERNEL);
	if (!conn)
		return -ENOMEM;
	conn->dev = &pdev->dev;
	platform_set_drvdata(pdev, conn);

	antiburn_read_u32(np, "support_elaboration_anti_strategy",
			  &conn->support_elaboration, 0);
	def_scale = conn->support_elaboration ? 1000 : 1;
	antiburn_read_u32(np, "trigger_temp", &conn->trigger_temp, 65 * def_scale);
	antiburn_read_u32(np, "recharge_temp", &conn->recharge_temp, 55 * def_scale);
	antiburn_read_u32(np, "comb_sensorboard_con_trigger_temp",
			  &conn->combined_board_trigger_temp, 60 * def_scale);
	antiburn_read_u32(np, "comb_rate_conn_trigger_temp",
			  &conn->combined_rate_trigger_temp, 35 * def_scale);
	antiburn_read_u32(np, "max_thermal_board_temp",
			  &conn->max_thermal_board_temp, 50);
	antiburn_read_u32(np, "max_temp_increase_rate",
			  &conn->max_temp_increase_rate, 4 * def_scale);
	antiburn_read_u32(np, "monitor_interval", &conn->monitor_interval,
			  ANTIBURN_INTERVAL_FAST_MS);
	antiburn_read_u32(np, "support_soft_antiburn", &conn->support_soft, 1);
	antiburn_read_u32(np, "support_hw_antiburn", &conn->support_hw, 1);
	antiburn_read_u32(np, "use_double_ntc", &conn->use_double_ntc, 0);
	antiburn_read_u32(np, "antiburn_otg_detect", &conn->otg_detect_en, 1);
	antiburn_read_u32(np, "otg_boost_src", &conn->otg_boost_src,
			  EXTERNAL_BOOST);
	antiburn_read_u32(np, "en_src", &conn->en_src, OTG_EN_BOOST);
	conn->support_base_flip = of_property_read_bool(np, "support-base-flip");

	/* Stock forces the external boost source on O2 build/minor revisions. */
	if (hwid->platform_version == 1 &&
	    (hwid->build_version == 0 ||
	     (hwid->build_version == 1 && hwid->minor_version == 0)))
		conn->otg_boost_src = EXTERNAL_BOOST;

	ret = of_property_read_string(np, "thermal-zone-name",
				      &conn->thermal_zone_name[0]);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "missing thermal-zone-name\n");
	if (conn->use_double_ntc) {
		ret = of_property_read_string(np, "thermal-zone-name2",
					      &conn->thermal_zone_name[1]);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "missing thermal-zone-name2\n");
	}

	conn->tzd[0] = thermal_zone_get_zone_by_name(conn->thermal_zone_name[0]);
	if (IS_ERR(conn->tzd[0]))
		return dev_err_probe(&pdev->dev, PTR_ERR(conn->tzd[0]),
				     "connector thermal zone unavailable\n");
	if (conn->use_double_ntc) {
		conn->tzd[1] = thermal_zone_get_zone_by_name(conn->thermal_zone_name[1]);
		if (IS_ERR(conn->tzd[1]))
			return dev_err_probe(&pdev->dev, PTR_ERR(conn->tzd[1]),
					     "second connector thermal zone unavailable\n");
	}

	conn->temperature[0] = antiburn_default_temp(conn);
	conn->temperature[1] = antiburn_default_temp(conn);
	if (conn->support_hw) {
		conn->mos_ctrl_gpio = of_get_named_gpio(np, "mos-ctrl-gpio", 0);
		if (!gpio_is_valid(conn->mos_ctrl_gpio))
			return dev_err_probe(&pdev->dev, conn->mos_ctrl_gpio,
					     "invalid anti-burn MOS gpio\n");
		ret = devm_gpio_request_one(&pdev->dev, conn->mos_ctrl_gpio,
					    GPIOF_OUT_INIT_LOW, "mos-ctrl-gpio");
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "anti-burn MOS gpio request failed\n");
	}

#ifdef CONFIG_SYSFS
	mca_sysfs_init_attrs(antiburn_attrs, antiburn_fields, ANTIBURN_ATTRS);
	ret = mca_sysfs_create_link_group(SYSFS_DEV_5, "connector", &pdev->dev,
					  &antiburn_group);
	if (ret)
		return ret;
#endif

	conn->thermal_nb.notifier_call = antiburn_thermal_event;
	conn->connect_nb.notifier_call = antiburn_connect_event;
	conn->debug_nb.notifier_call = antiburn_debug_event;
	(void)mca_event_block_notify_register(MCA_EVENT_TYPE_THERMAL_TEMP,
					      &conn->thermal_nb);
	(void)mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					      &conn->connect_nb);
	(void)mca_event_block_notify_register(MCA_EVENT_TYPE_SUBPMIC_INFO,
					      &conn->debug_nb);

	INIT_DELAYED_WORK(&conn->monitor_work, antiburn_monitor_workfn);
	schedule_delayed_work(&conn->monitor_work,
			      msecs_to_jiffies(conn->monitor_interval));
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_USCP,
				    &antiburn_log_ops, conn);
	g_conn = conn;
	mca_log_info("ready elaboration=%d trigger=%d recharge=%d rate=%d\n",
		     conn->support_elaboration, conn->trigger_temp,
		     conn->recharge_temp, conn->max_temp_increase_rate);
	return 0;
}

static int antiburn_remove(struct platform_device *pdev)
{
	struct connector_antiburn *conn = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&conn->monitor_work);
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_SUBPMIC_INFO,
						&conn->debug_nb);
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
						&conn->connect_nb);
	(void)mca_event_block_notify_unregister(MCA_EVENT_TYPE_THERMAL_TEMP,
						&conn->thermal_nb);
#ifdef CONFIG_SYSFS
	mca_sysfs_remove_link_group(SYSFS_DEV_5, "connector", &pdev->dev,
				    &antiburn_group);
#endif
	antiburn_set_mos(conn, false);
	if (g_conn == conn)
		g_conn = NULL;
	return 0;
}

static const struct of_device_id antiburn_match[] = {
	{ .compatible = "xiaomi,connector_antiburn" },
	{},
};
MODULE_DEVICE_TABLE(of, antiburn_match);

static struct platform_driver antiburn_driver = {
	.driver = {
		.name = "connector_antiburn",
		.of_match_table = antiburn_match,
	},
	.probe = antiburn_probe,
	.remove = antiburn_remove,
};
module_platform_driver(antiburn_driver);

MODULE_DESCRIPTION("Xiaomi Dada MCA connector anti-burn protection");
MODULE_LICENSE("GPL v2");
