#include <mca/common/mca_workqueue.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/ktime.h>
#include <linux/pm_wakeup.h>
#include <linux/soc/qcom/battery_charger.h>

#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_adsp_glink.h>
#include <mca/platform/platform_bc12_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_qc_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/strategy/strategy_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "qcom_subpmic"
#endif

/* OS2.0.101.0 Dada wire IDs: Onyx-only enum entries must not shift these. */
enum mca_subpmic_prop {
	SUBPMIC_PROP_SHIP_MODE = 5,
	SUBPMIC_PROP_SHUTDOWN = 6,
	SUBPMIC_PROP_WLS_INPUT_CURR_LIMIT = 0x1003,
	SUBPMIC_PROP_WLS_INPUT_SUSPEND = 0x1004,
	SUBPMIC_PROP_WLS_CURR = 0x1007,
	SUBPMIC_PROP_WLS_VDD_FLAG = 0x100c,
	SUBPMIC_PROP_CHG_STATUS = 0x2001,
	SUBPMIC_PROP_CHG_TYPE = 0x2002,
	SUBPMIC_PROP_ENABLE_CHARGING = 0x2003,
	SUBPMIC_PROP_CHARGE_CURRENT = 0x2004,
	SUBPMIC_PROP_TERM_CURRENT = 0x2005,
	SUBPMIC_PROP_TERM_VOLT = 0x2006,
	SUBPMIC_PROP_INPUT_CURR_LIMIT = 0x2007,
	SUBPMIC_PROP_INPUT_VOLT_LIMIT = 0x2008,
	SUBPMIC_PROP_PRECHG_CURRENT = 0x2009,
	SUBPMIC_PROP_PRECHG_VOLT = 0x200a,
	SUBPMIC_PROP_VSYS_VOLT = 0x200b,
	SUBPMIC_PROP_OTG_BOOST_EN_STATUS = 0x200c,
	SUBPMIC_PROP_OTG_GATE_EN_STATUS = 0x200d,
	SUBPMIC_PROP_BOOST_ENABLE = 0x200e,
	SUBPMIC_PROP_BOOST_VOLTAGE = 0x200f,
	SUBPMIC_PROP_QC_VOLT = 0x2010,
	SUBPMIC_PROP_AICL_ENABLE = 0x2011,
	SUBPMIC_PROP_RERUN_AICL = 0x2012,
	SUBPMIC_PROP_USB_AICL_CONT_THD = 0x2013,
	SUBPMIC_PROP_OPT_FWS = 0x2014,
	SUBPMIC_PROP_USB_ADAP_OVERRIDE = 0x2015,
	SUBPMIC_PROP_QC3_VOLT = 0x2016,
	SUBPMIC_PROP_INPUT_SUSPEND = 0x2017,
	SUBPMIC_PROP_OTG_CFG = 0x2018,
	SUBPMIC_PROP_CID_CFG = 0x2019,
	SUBPMIC_PROP_CP_STATE = 0x201a,
	SUBPMIC_PROP_QC_VOLT_CMD = 0x201b,
	SUBPMIC_PROP_LPD_SBU1 = 0x201c,
	SUBPMIC_PROP_LPD_SBU2 = 0x201d,
	SUBPMIC_PROP_LPD_CC1 = 0x201e,
	SUBPMIC_PROP_LPD_CC2 = 0x201f,
	SUBPMIC_PROP_LPD_DP = 0x2020,
	SUBPMIC_PROP_LPD_DM = 0x2021,
	SUBPMIC_PROP_LPD_CONTROL = 0x2022,
	SUBPMIC_PROP_LPD_UART_CONTROL = 0x2023,
	SUBPMIC_PROP_PACK_VBAT = 0x2024,
	SUBPMIC_PROP_EU_MODEL = 0x2025,
	SUBPMIC_PROP_USB_ONLINE = 0x20001,
	SUBPMIC_PROP_BUS_VOLT = 0x20002,
	SUBPMIC_PROP_USB_SNS_VOLT = 0x20003,
	SUBPMIC_PROP_BUS_CURR = 0x20004,
	SUBPMIC_PROP_LPD_ENABLE = 0x20009,
	SUBPMIC_PROP_LPD_STATUS = 0x2000a,
	SUBPMIC_PROP_USB_REAL_TYPE = 0x2000c,
};

enum mca_subpmic_notify {
	SUBPMIC_NOTIFY_USB_TYPE = 0,
	SUBPMIC_NOTIFY_CHARGE_TYPE = 2,
	SUBPMIC_NOTIFY_ENABLE_BOOST = 3,
	SUBPMIC_NOTIFY_TYPEC_CHANGE = 5,
	SUBPMIC_NOTIFY_LPD_STATUS = 4,
	SUBPMIC_NOTIFY_CC_SHORT_VBUS = 6,
	SUBPMIC_NOTIFY_PPS_PTF = 7,
	SUBPMIC_NOTIFY_SINK_PWR_SUSPEND = 8,
};


#define SUBPMIC_QC_VOLT_MIN 5000
#define SUBPMIC_QC_VOLT_MAX 12000

struct qcom_subpmic_otg_cfg {
	u32 otg_boost_src;
	u32 gpio_chip_type;
	u32 vdd_boost_en;
	u32 otg_ovp_en;
};

struct qcom_subpmic {
	struct device *dev;
	struct delayed_work update_usb_type_work;
	struct work_struct notify_change_work;
	struct delayed_work sync_cfg_work;
	struct notifier_block ship_mode_nb;
	struct notifier_block shutdown_nb;
	struct list_head notify_list;
	spinlock_t notify_lock;
	bool support_2s_charging;
	bool support_dual_panel;
	bool support_multi_bc12;
	bool support_cid;
	u32 cid_gpio_int;
	struct qcom_subpmic_otg_cfg otg_cfg;
	bool is_enable_shipmode;
	u8 eu_model;
	int online;
	int real_type;
	int glink_down;
	int init_ok;
	int lpd_status;
	int cc_short_vbus;
	int pps_ptf;
	u8 sink_pwr_suspend;
};

struct qcom_subpmic_notify_entry {
	struct list_head node;
	int type;
	int val;
};

static RAW_NOTIFIER_HEAD(hboost_notifier);
static ATOMIC_NOTIFIER_HEAD(pen_charge_state_notifier);

int pen_charge_state_notifier_register_client(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&pen_charge_state_notifier, nb);
}
EXPORT_SYMBOL(pen_charge_state_notifier_register_client);

int pen_charge_state_notifier_unregister_client(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&pen_charge_state_notifier, nb);
}
EXPORT_SYMBOL(pen_charge_state_notifier_unregister_client);

int mca_pen_charge_state_notify(unsigned long event, void *value)
{
	return atomic_notifier_call_chain(&pen_charge_state_notifier, event, value);
}
EXPORT_SYMBOL(mca_pen_charge_state_notify);

int register_hboost_event_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&hboost_notifier, nb);
}
EXPORT_SYMBOL(register_hboost_event_notifier);

int unregister_hboost_event_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&hboost_notifier, nb);
}
EXPORT_SYMBOL(unregister_hboost_event_notifier);

int qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val)
{
	if (!val)
		return -EINVAL;
	*val = (prop_id != 0) ? 0 : 100000;
	return 0;
}
EXPORT_SYMBOL(qti_battery_charger_get_prop);

static int qcom_subpmic_get_usb_real_type(int *type, void *data)
{
	struct qcom_subpmic *sc = data;
	int rc;

	rc = mca_adsp_glink_read_prop(SUBPMIC_PROP_USB_REAL_TYPE, type, sizeof(*type));
	if (rc != 0)
		*type = sc->real_type;
	return 0;
}

static int qcom_subpmic_bc12_det_en(int en, void *data)
{
	return 0;
}

static int qcom_subpmic_set_qc_volt(void *data, int volt)
{
	struct qcom_subpmic *sc = data;
	ktime_t start = ktime_get_boottime();
	int volt_cmd = volt, vbus = 0, real_type = sc->real_type;
	int step, target, delta, per_step_ms, rc;
	unsigned int settle_thd, step_count;

	settle_thd = (real_type != XM_CHARGER_TYPE_HVDCP3P5) ? 200 : 20;
	per_step_ms = (real_type != XM_CHARGER_TYPE_HVDCP3P5) ? 50 : 5;
	if (volt < SUBPMIC_QC_VOLT_MIN || volt > SUBPMIC_QC_VOLT_MAX)
		return -EINVAL;
	step = (real_type != XM_CHARGER_TYPE_HVDCP3P5) ? 800 : 500;
	mca_adsp_glink_read_prop(SUBPMIC_PROP_BUS_VOLT, &vbus, sizeof(vbus));
	vbus /= 1000;
	delta = volt - vbus;
	target = volt + ((delta == 0 || volt < vbus) ? step : -step);
	rc = mca_adsp_glink_write_prop(SUBPMIC_PROP_QC_VOLT, &volt_cmd,
				       sizeof(volt_cmd));
	if (rc)
		return rc;
	if (sc->real_type == XM_CHARGER_TYPE_HVDCP2 || volt_cmd == 5000)
		return 0;
	if (abs(delta) < (int)settle_thd)
		return 0;
	step_count = settle_thd ? abs(delta) / settle_thd : 0;
	do {
		msleep(50);
		mca_adsp_glink_read_prop(SUBPMIC_PROP_BUS_VOLT, &vbus, sizeof(vbus));
		vbus /= 1000;
		if (vbus < 4000 || sc->online == 0)
			break;
		if (((target < vbus && delta != 0) && (vbus <= target || delta >= 0)) ||
		    (vbus < target && delta < 0))
			break;
	} while (ktime_ms_delta(ktime_get_boottime(), start) <=
		 (s64)(step_count * per_step_ms));
	return 0;
}

static int qcom_subpmic_set_qc_volt_cmd(void *data, int cmd)
{
	int val = cmd;
	return mca_adsp_glink_write_prop(SUBPMIC_PROP_QC_VOLT_CMD, &val, sizeof(val));
}

#define SUBPMIC_GETTER(_name, _prop) \
static int qcom_subpmic_get_##_name(void *data, int *val) \
{ return mca_adsp_glink_read_prop((_prop), val, sizeof(*val)); }

SUBPMIC_GETTER(bus_volt, SUBPMIC_PROP_BUS_VOLT)
SUBPMIC_GETTER(bus_curr, SUBPMIC_PROP_BUS_CURR)
SUBPMIC_GETTER(usb_sns_volt, SUBPMIC_PROP_USB_SNS_VOLT)
SUBPMIC_GETTER(vsys_volt, SUBPMIC_PROP_VSYS_VOLT)
SUBPMIC_GETTER(chg_status, SUBPMIC_PROP_CHG_STATUS)
SUBPMIC_GETTER(chg_type, SUBPMIC_PROP_CHG_TYPE)
SUBPMIC_GETTER(term_current, SUBPMIC_PROP_TERM_CURRENT)
SUBPMIC_GETTER(term_volt, SUBPMIC_PROP_TERM_VOLT)
SUBPMIC_GETTER(wls_curr, SUBPMIC_PROP_WLS_CURR)
SUBPMIC_GETTER(input_current_limit, SUBPMIC_PROP_INPUT_CURR_LIMIT)
SUBPMIC_GETTER(usb_aicl_cont_thd, SUBPMIC_PROP_USB_AICL_CONT_THD)
SUBPMIC_GETTER(lpd_enable, SUBPMIC_PROP_LPD_ENABLE)
SUBPMIC_GETTER(lpd_status, SUBPMIC_PROP_LPD_STATUS)
SUBPMIC_GETTER(lpd_sbu1, SUBPMIC_PROP_LPD_SBU1)
SUBPMIC_GETTER(lpd_sbu2, SUBPMIC_PROP_LPD_SBU2)
SUBPMIC_GETTER(lpd_cc1, SUBPMIC_PROP_LPD_CC1)
SUBPMIC_GETTER(lpd_cc2, SUBPMIC_PROP_LPD_CC2)
SUBPMIC_GETTER(lpd_dp, SUBPMIC_PROP_LPD_DP)
SUBPMIC_GETTER(lpd_dm, SUBPMIC_PROP_LPD_DM)
SUBPMIC_GETTER(lpd_control, SUBPMIC_PROP_LPD_CONTROL)
SUBPMIC_GETTER(lpd_uart_control, SUBPMIC_PROP_LPD_UART_CONTROL)
SUBPMIC_GETTER(pack_vbat, SUBPMIC_PROP_PACK_VBAT)
SUBPMIC_GETTER(otg_gate_enable_status, SUBPMIC_PROP_OTG_GATE_EN_STATUS)
SUBPMIC_GETTER(otg_boost_enable_status, SUBPMIC_PROP_OTG_BOOST_EN_STATUS)

SUBPMIC_GETTER(otg_enable_status, SUBPMIC_PROP_OTG_BOOST_EN_STATUS)
SUBPMIC_GETTER(otg_plugin_status, SUBPMIC_PROP_OTG_GATE_EN_STATUS)

static int qcom_subpmic_get_otg_status(void *data, bool *status)
{
	int state = 0;
	int ret = qcom_subpmic_get_otg_enable_status(data, &state);

	if (!ret)
		*status = state == 2;
	return ret;
}

static int qcom_subpmic_get_online(void *data, int *online)
{
	*online = ((struct qcom_subpmic *)data)->online;
	return 0;
}

static int qcom_subpmic_get_otg_boost_src(void *data, int *src)
{
	*src = ((struct qcom_subpmic *)data)->otg_cfg.otg_boost_src;
	return *src;
}

static int qcom_subpmic_is_charge_done(void *data, bool *done)
{
	int status = 0;
	int rc = mca_adsp_glink_read_prop(SUBPMIC_PROP_CHG_STATUS, &status,
					  sizeof(status));
	if (rc < 0)
		return rc;
	*done = (status == 5);
	return 0;
}

static int qcom_subpmic_is_init_ok(void *data)
{
	struct qcom_subpmic *sc = data;
	return (!sc || sc->init_ok == 0) ? -1 : sc->init_ok;
}

static int qcom_subpmic_is_support_cid(void *data, bool *en)
{
	*en = ((struct qcom_subpmic *)data)->support_cid;
	return 0;
}

static int qcom_subpmic_get_ship_mode(void *data, bool *en)
{
	*en = ((struct qcom_subpmic *)data)->is_enable_shipmode;
	return 0;
}

#define SUBPMIC_SET_INT(_name, _prop) \
static int qcom_subpmic_set_##_name(void *data, int value) \
{ int val = value; return mca_adsp_glink_write_prop((_prop), &val, sizeof(val)); }
#define SUBPMIC_SET_BOOL(_name, _prop) \
static int qcom_subpmic_set_##_name(void *data, bool value) \
{ u8 val = value; return mca_adsp_glink_write_prop((_prop), &val, sizeof(val)); }

SUBPMIC_SET_BOOL(input_suspend, SUBPMIC_PROP_INPUT_SUSPEND)
SUBPMIC_SET_BOOL(wireless_input_suspend, SUBPMIC_PROP_WLS_INPUT_SUSPEND)
SUBPMIC_SET_INT(input_current_limit, SUBPMIC_PROP_INPUT_CURR_LIMIT)
SUBPMIC_SET_INT(wls_input_current_limit, SUBPMIC_PROP_WLS_INPUT_CURR_LIMIT)
SUBPMIC_SET_INT(input_volt_limit, SUBPMIC_PROP_INPUT_VOLT_LIMIT)
SUBPMIC_SET_INT(charge_current, SUBPMIC_PROP_CHARGE_CURRENT)
SUBPMIC_SET_BOOL(enable_charging, SUBPMIC_PROP_ENABLE_CHARGING)
SUBPMIC_SET_INT(term_current, SUBPMIC_PROP_TERM_CURRENT)
SUBPMIC_SET_INT(term_volt, SUBPMIC_PROP_TERM_VOLT)
SUBPMIC_SET_INT(prechg_volt, SUBPMIC_PROP_PRECHG_VOLT)
SUBPMIC_SET_INT(prechg_current, SUBPMIC_PROP_PRECHG_CURRENT)
SUBPMIC_SET_INT(usb_aicl_cont_thd, SUBPMIC_PROP_USB_AICL_CONT_THD)
SUBPMIC_SET_INT(opt_fws, SUBPMIC_PROP_OPT_FWS)
SUBPMIC_SET_BOOL(usb_adapter_allow_override, SUBPMIC_PROP_USB_ADAP_OVERRIDE)
SUBPMIC_SET_INT(qc3_volt, SUBPMIC_PROP_QC3_VOLT)
SUBPMIC_SET_INT(boost_enable, SUBPMIC_PROP_BOOST_ENABLE)
SUBPMIC_SET_INT(boost_voltage, SUBPMIC_PROP_BOOST_VOLTAGE)
SUBPMIC_SET_BOOL(aicl_enable, SUBPMIC_PROP_AICL_ENABLE)
SUBPMIC_SET_BOOL(rerun_aicl, SUBPMIC_PROP_RERUN_AICL)
SUBPMIC_SET_BOOL(wls_vdd_flag, SUBPMIC_PROP_WLS_VDD_FLAG)
SUBPMIC_SET_INT(lpd_sbu1, SUBPMIC_PROP_LPD_SBU1)
SUBPMIC_SET_INT(lpd_control, SUBPMIC_PROP_LPD_CONTROL)
SUBPMIC_SET_INT(lpd_uart_control, SUBPMIC_PROP_LPD_UART_CONTROL)

static int qcom_subpmic_set_ship_mode(void *data, bool en)
{
	((struct qcom_subpmic *)data)->is_enable_shipmode = en;
	return 0;
}

static int qcom_subpmic_set_eu_model(void *data, bool en)
{
	struct qcom_subpmic *sc = data;
	u8 val = en;
	int rc = mca_adsp_glink_write_prop(SUBPMIC_PROP_EU_MODEL, &val, sizeof(val));
	if (rc < 0)
		return rc;
	sc->eu_model = val;
	return 0;
}

static struct protocol_class_qc_ops g_adsp_qc_ops = {
	.protocol_qc_get_qc_type = qcom_subpmic_get_usb_real_type,
	.protocol_qc_set_volt = qcom_subpmic_set_qc_volt,
	.protocol_qc_set_volt_cmd = qcom_subpmic_set_qc_volt_cmd,
};

static struct platform_bc12_class_ops g_qcom_pmic_bc12_ops = {
	.bc12_det_en = qcom_subpmic_bc12_det_en,
	.get_charge_type = qcom_subpmic_get_usb_real_type,
};

static struct platform_class_buckchg_ops g_qcom_buckchg_ops = {
	.is_init_ok = qcom_subpmic_is_init_ok,
	.get_online = qcom_subpmic_get_online,
	.get_otg_status = qcom_subpmic_get_otg_status,
	.get_otg_enable_status = qcom_subpmic_get_otg_enable_status,
	.get_otg_plugin_status = qcom_subpmic_get_otg_plugin_status,
	.is_charge_done = qcom_subpmic_is_charge_done,
	.get_input_curr_lmt = qcom_subpmic_get_input_current_limit,
	.get_bus_curr = qcom_subpmic_get_bus_curr,
	.get_bus_volt = qcom_subpmic_get_bus_volt,
	.get_usb_sns_volt = qcom_subpmic_get_usb_sns_volt,
	.get_sys_volt = qcom_subpmic_get_vsys_volt,
	.get_chg_status = qcom_subpmic_get_chg_status,
	.get_chg_type = qcom_subpmic_get_chg_type,
	.get_term_curr = qcom_subpmic_get_term_current,
	.get_term_volt = qcom_subpmic_get_term_volt,
	.get_wls_curr = qcom_subpmic_get_wls_curr,
	.set_hiz = qcom_subpmic_set_input_suspend,
	.set_wls_hiz = qcom_subpmic_set_wireless_input_suspend,
	.set_input_curr_lmt = qcom_subpmic_set_input_current_limit,
	.set_wls_input_curr_lmt = qcom_subpmic_set_wls_input_current_limit,
	.set_input_volt_lmt = qcom_subpmic_set_input_volt_limit,
	.set_ichg = qcom_subpmic_set_charge_current,
	.set_chg = qcom_subpmic_set_enable_charging,
	.set_term_curr = qcom_subpmic_set_term_current,
	.set_term_volt = qcom_subpmic_set_term_volt,
	.set_prechg_volt = qcom_subpmic_set_prechg_volt,
	.set_prechg_curr = qcom_subpmic_set_prechg_current,
	.set_qc_volt = qcom_subpmic_set_qc_volt,
	.set_usb_aicl_cont_thd = qcom_subpmic_set_usb_aicl_cont_thd,
	.get_usb_aicl_cont_thd = qcom_subpmic_get_usb_aicl_cont_thd,
	.set_opt_fws = qcom_subpmic_set_opt_fws,
	.usb_adapter_allow_override = qcom_subpmic_set_usb_adapter_allow_override,
	.set_qc3_volt = qcom_subpmic_set_qc3_volt,
	.get_otg_boost_src = qcom_subpmic_get_otg_boost_src,
	.get_otg_boost_enable_status = qcom_subpmic_get_otg_boost_enable_status,
	.get_otg_gate_enable_status = qcom_subpmic_get_otg_gate_enable_status,
	.set_boost_enable = qcom_subpmic_set_boost_enable,
	.set_boost_voltage = qcom_subpmic_set_boost_voltage,
	.set_aicl_enable = qcom_subpmic_set_aicl_enable,
	.set_rerun_aicl = qcom_subpmic_set_rerun_aicl,
	.is_support_cid = qcom_subpmic_is_support_cid,
	.set_ship_mode = qcom_subpmic_set_ship_mode,
	.get_ship_mode = qcom_subpmic_get_ship_mode,
	.set_wls_vdd_flag = qcom_subpmic_set_wls_vdd_flag,
	.get_lpd_enable = qcom_subpmic_get_lpd_enable,
	.get_lpd_status = qcom_subpmic_get_lpd_status,
	.get_lpd_sbu1 = qcom_subpmic_get_lpd_sbu1,
	.get_lpd_sbu2 = qcom_subpmic_get_lpd_sbu2,
	.get_lpd_cc1 = qcom_subpmic_get_lpd_cc1,
	.get_lpd_cc2 = qcom_subpmic_get_lpd_cc2,
	.get_lpd_dp = qcom_subpmic_get_lpd_dp,
	.get_lpd_dm = qcom_subpmic_get_lpd_dm,
	.set_lpd_sbu1 = qcom_subpmic_set_lpd_sbu1,
	.set_lpd_control = qcom_subpmic_set_lpd_control,
	.get_lpd_control = qcom_subpmic_get_lpd_control,
	.set_lpd_uart_control = qcom_subpmic_set_lpd_uart_control,
	.get_lpd_uart_control = qcom_subpmic_get_lpd_uart_control,
	.get_pack_vbat = qcom_subpmic_get_pack_vbat,
	.set_eu_model = qcom_subpmic_set_eu_model,
};

static void qcom_subpmic_update_usb_type_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct qcom_subpmic *sc = container_of(to_delayed_work(work),
					       struct qcom_subpmic, update_usb_type_work);
	int usb_online = 0, real_type = 0, pd_active = 0, rc;

	if (sc->support_multi_bc12) {
		protocol_class_pd_get_pd_active(1, &pd_active);
		if (pd_active != 0)
			return;
	}
	rc = mca_adsp_glink_read_prop(SUBPMIC_PROP_USB_ONLINE, &usb_online,
				      sizeof(usb_online));
	if (rc < 0)
		goto retry;
	if (sc->init_ok == 0) {
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_PMIC_INIT_DONE, NULL);
		sc->init_ok = 1;
	}
	if (sc->online != usb_online) {
		sc->online = usb_online;
		if (!usb_online) {
			protocol_class_pd_set_pd_active(0, 0);
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT,
					       MCA_EVENT_USB_DISCONNECT, NULL);
			pm_relax(sc->dev);
		} else {
			protocol_class_pd_set_pd_active(0, 1);
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT,
					       MCA_EVENT_USB_CONNECT, NULL);
		}
	}
	rc = mca_adsp_glink_read_prop(SUBPMIC_PROP_USB_REAL_TYPE, &real_type,
				      sizeof(real_type));
	if (rc < 0)
		goto retry;
	if (sc->real_type == real_type && sc->glink_down == 0)
		return;
	sc->real_type = real_type;
	sc->glink_down = 0;
	mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_TYPE,
			       MCA_EVENT_CHARGE_TYPE_CHANGE, &sc->real_type);
	return;
retry:
	queue_delayed_work(system_wq, &sc->update_usb_type_work,
			   msecs_to_jiffies(500));
}

static int qcom_subpmic_sync_cfg_work_count;

static void qcom_subpmic_sync_cfg_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct qcom_subpmic *sc = container_of(to_delayed_work(work),
					       struct qcom_subpmic, sync_cfg_work);
	bool cp_present = false;
	int rc;

	rc = platform_class_cp_get_present(0, &cp_present);
	if (rc)
		goto retry;
	rc = mca_adsp_glink_write_prop(SUBPMIC_PROP_CP_STATE, &cp_present,
				       sizeof(cp_present));
	if (rc)
		goto retry;
	rc = mca_adsp_glink_write_prop(SUBPMIC_PROP_OTG_CFG, &sc->otg_cfg,
				       sizeof(sc->otg_cfg));
	if (rc)
		goto retry;
	if (!sc->support_cid)
		return;
	rc = mca_adsp_glink_write_prop(SUBPMIC_PROP_CID_CFG, &sc->cid_gpio_int,
				       sizeof(sc->cid_gpio_int));
	if (rc)
		goto retry;
	rc = mca_adsp_glink_write_prop(SUBPMIC_PROP_EU_MODEL, &sc->eu_model,
				       sizeof(sc->eu_model));
	if (!rc)
		return;
retry:
	if (qcom_subpmic_sync_cfg_work_count++ < 0x3c)
		queue_delayed_work(system_wq, &sc->sync_cfg_work, msecs_to_jiffies(250));
}

static void qcom_subpmic_notify_change_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	struct qcom_subpmic *sc = container_of(work, struct qcom_subpmic,
					       notify_change_work);
	struct qcom_subpmic_notify_entry *entry, *tmp;

	spin_lock(&sc->notify_lock);
	list_for_each_entry_safe(entry, tmp, &sc->notify_list, node) {
		list_del(&entry->node);
		spin_unlock(&sc->notify_lock);
		switch (entry->type) {
		case SUBPMIC_NOTIFY_TYPEC_CHANGE:
			mca_event_block_notify(MCA_EVENT_TYPE_TYPEC_PORT_STATUS,
					       MCA_EVENT_TYPEC_PORT_CHANGE, NULL);
			break;
		case SUBPMIC_NOTIFY_ENABLE_BOOST:
			platform_class_cp_enable_ovpgate_with_check(0, 0,
							       entry->val == 0);
			break;
		case SUBPMIC_NOTIFY_CHARGE_TYPE:
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_TYPE,
					       MCA_EVENT_CHARGE_CAP_CHANGE, NULL);
			break;
		}
		kfree(entry);
		spin_lock(&sc->notify_lock);
	}
	spin_unlock(&sc->notify_lock);
}

static void qcom_subpmic_queue_notify(struct qcom_subpmic *sc, int type, int val)
{
	struct qcom_subpmic_notify_entry *entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return;
	entry->type = type;
	entry->val = val;
	spin_lock(&sc->notify_lock);
	list_add_tail(&entry->node, &sc->notify_list);
	spin_unlock(&sc->notify_lock);
	queue_work(system_wq, &sc->notify_change_work);
}

static void qcom_subpmic_report_uevent(const char *fmt, int val)
{
	struct mca_event_notify_data n_data;
	char buf[128] = { 0 };
	n_data.event_len = snprintf(buf, sizeof(buf), fmt, val);
	n_data.event = buf;
	mca_event_report_uevent(&n_data);
}

static void qcom_subpmic_glink_down_cb(void *priv)
{
	struct qcom_subpmic *sc = priv;
	sc->glink_down = 1;
	mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
			       MCA_EVENT_CHARGE_ABNORMAL, NULL);
}

static void qcom_subpmic_sync_cb(void *priv)
{
	struct qcom_subpmic *sc = priv;
	mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
			       MCA_EVENT_CHARGE_RESTORE, NULL);
	queue_delayed_work(system_wq, &sc->update_usb_type_work, 0);
	queue_delayed_work(system_wq, &sc->sync_cfg_work, 0);
}

static void qcom_subpmic_notify_cb(u32 notify_type, void *data, u32 len,
				   void *priv)
{
	struct qcom_subpmic *sc = priv;
	int *val = data;
	if (!sc)
		return;
	pm_wakeup_dev_event(sc->dev, 1000, true);
	switch (notify_type) {
	case SUBPMIC_NOTIFY_USB_TYPE:
		queue_delayed_work(system_wq, &sc->update_usb_type_work, 0);
		break;
	case SUBPMIC_NOTIFY_CHARGE_TYPE:
	case SUBPMIC_NOTIFY_ENABLE_BOOST:
	case SUBPMIC_NOTIFY_TYPEC_CHANGE:
		qcom_subpmic_queue_notify(sc, notify_type, val ? *val : 0);
		break;
	case SUBPMIC_NOTIFY_LPD_STATUS:
		if (!val)
			break;
		sc->lpd_status = *val;
		qcom_subpmic_report_uevent("POWER_SUPPLY_MOISTURE_DET_STS=%d",
					   sc->lpd_status);
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO,
				       MCA_EVENT_LPD_STATUS_CHANGE, &sc->lpd_status);
		break;
	case SUBPMIC_NOTIFY_CC_SHORT_VBUS:
		if (!val)
			break;
		sc->cc_short_vbus = *val;
		qcom_subpmic_report_uevent("POWER_SUPPLY_CC_SHORT_VBUS=%d",
					   sc->cc_short_vbus);
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_CC_SHORT_VBUS,
				       &sc->cc_short_vbus);
		break;
	case SUBPMIC_NOTIFY_PPS_PTF:
		if (!val)
			break;
		sc->pps_ptf = *val;
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					  MCA_EVENT_PPS_PTF, sc->pps_ptf);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					  MCA_EVENT_PPS_PTF, sc->pps_ptf);
		break;
	case SUBPMIC_NOTIFY_SINK_PWR_SUSPEND:
		if (val && sc->eu_model) {
			sc->sink_pwr_suspend = *(u8 *)data;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_TYPE,
					       MCA_EVENT_SINK_PWR_SUSPEND_CHANGE,
					       &sc->sink_pwr_suspend);
		}
		break;
	}
}

static struct mca_adsp_glink_ops g_qcom_subpmic_glink_cb = {
	.glink_state_down = qcom_subpmic_glink_down_cb,
	.glink_state_up = qcom_subpmic_sync_cb,
	.notification = qcom_subpmic_notify_cb,
};

static int qcom_subpmic_ship_mode(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct qcom_subpmic *sc = container_of(nb, struct qcom_subpmic, ship_mode_nb);
	u64 reason = action;
	int chip_vendor = -1;
	if ((action & ~2UL) == 1 && sc->is_enable_shipmode) {
		platform_class_cp_get_chip_vendor(0, &chip_vendor);
		if (chip_vendor != 0)
			platform_class_cp_enable_ovpgate(0, 0);
		mca_adsp_glink_write_prop(SUBPMIC_PROP_SHIP_MODE, &reason, sizeof(reason));
	}
	return NOTIFY_DONE;
}

static int qcom_subpmic_shutdown_cb(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	u64 reason = action;
	if ((action & ~2UL) == 1)
		mca_adsp_glink_write_prop(SUBPMIC_PROP_SHUTDOWN, &reason, sizeof(reason));
	return NOTIFY_DONE;
}

static void qcom_subpmic_parse_dt(struct qcom_subpmic *sc,
				  struct device_node *node)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	sc->support_2s_charging = of_find_property(node, "mi,support-2s-charging", NULL);
	sc->support_dual_panel = of_find_property(node, "mi,support-dual-panel", NULL);
	sc->support_multi_bc12 = of_find_property(node, "mi,support-multi-bc12", NULL);
	sc->support_cid = of_find_property(node, "mi,support-cid", NULL);
	mca_parse_dts_u32(node, "otg_boost_src", &sc->otg_cfg.otg_boost_src, 2);
	mca_parse_dts_u32(node, "gpio_chip_type", &sc->otg_cfg.gpio_chip_type, 1);
	mca_parse_dts_u32(node, "vdd_boost_en", &sc->otg_cfg.vdd_boost_en, 0xffffffff);
	mca_parse_dts_u32(node, "otg_ovp_en", &sc->otg_cfg.otg_ovp_en, 0xffffffff);
	if (hwid && hwid->platform_version == 1 &&
	    (hwid->build_version == 0 ||
	     (hwid->build_version == 1 && hwid->minor_version == 0)))
		sc->otg_cfg.otg_boost_src = 2;
	if (sc->support_cid)
		mca_parse_dts_u32(node, "cid_gpio_int", &sc->cid_gpio_int, 0);
}

static int qcom_subpmic_probe(struct platform_device *pdev)
{
	struct qcom_subpmic *sc;
	int rc;

	sc = devm_kzalloc(&pdev->dev, sizeof(*sc), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;
	sc->dev = &pdev->dev;
	if (!mca_get_hwid_info())
		return -EPROBE_DEFER;
	qcom_subpmic_parse_dt(sc, pdev->dev.of_node);
	INIT_LIST_HEAD(&sc->notify_list);
	spin_lock_init(&sc->notify_lock);
	INIT_DELAYED_WORK(&sc->update_usb_type_work, qcom_subpmic_update_usb_type_work);
	INIT_DELAYED_WORK(&sc->sync_cfg_work, qcom_subpmic_sync_cfg_work);
	INIT_WORK(&sc->notify_change_work, qcom_subpmic_notify_change_work);
	sc->ship_mode_nb.notifier_call = qcom_subpmic_ship_mode;
	sc->ship_mode_nb.priority = 255;
	register_reboot_notifier(&sc->ship_mode_nb);
	sc->shutdown_nb.notifier_call = qcom_subpmic_shutdown_cb;
	sc->shutdown_nb.priority = 255;
	register_reboot_notifier(&sc->shutdown_nb);
	device_set_wakeup_capable(sc->dev, true);
	device_wakeup_enable(sc->dev);
	platform_set_drvdata(pdev, sc);
	rc = platform_bc12_class_ops_register(0, &g_qcom_pmic_bc12_ops, sc);
	if (rc)
		goto err;
	rc = protocol_class_qc_register_ops(0, &g_adsp_qc_ops, sc);
	if (rc)
		goto err;
	rc = platform_class_buckchg_ops_register(0, sc, &g_qcom_buckchg_ops);
	if (rc)
		goto err;
	rc = mca_adsp_glink_resister_ops(&g_qcom_subpmic_glink_cb, sc);
	if (rc)
		goto err;
	queue_delayed_work(system_wq, &sc->update_usb_type_work, 0);
	queue_delayed_work(system_wq, &sc->sync_cfg_work, msecs_to_jiffies(25));
	return 0;
err:
	mca_adsp_glink_unregister_ops(sc);
	platform_class_buckchg_ops_unregister(0, sc);
	protocol_class_qc_unregister_ops(0, sc);
	platform_bc12_class_ops_unregister(0, sc);
	unregister_reboot_notifier(&sc->ship_mode_nb);
	unregister_reboot_notifier(&sc->shutdown_nb);
	return rc;
}

static int qcom_subpmic_remove(struct platform_device *pdev)
{
	struct qcom_subpmic *sc = platform_get_drvdata(pdev);
	if (!sc)
		return 0;
	mca_adsp_glink_unregister_ops(sc);
	platform_class_buckchg_ops_unregister(0, sc);
	protocol_class_qc_unregister_ops(0, sc);
	platform_bc12_class_ops_unregister(0, sc);
	cancel_delayed_work_sync(&sc->update_usb_type_work);
	cancel_delayed_work_sync(&sc->sync_cfg_work);
	cancel_work_sync(&sc->notify_change_work);
	unregister_reboot_notifier(&sc->ship_mode_nb);
	unregister_reboot_notifier(&sc->shutdown_nb);
	return 0;
}

static const struct of_device_id qcom_subpmic_match_table[] = {
	{ .compatible = "mca,qcom_subpmic" },
	{},
};
MODULE_DEVICE_TABLE(of, qcom_subpmic_match_table);

static struct platform_driver qcom_subpmic_driver = {
	.driver = {
		.name = "qcom_subpmic",
		.of_match_table = qcom_subpmic_match_table,
	},
	.probe = qcom_subpmic_probe,
	.remove = qcom_subpmic_remove,
};
module_platform_driver(qcom_subpmic_driver);

MODULE_DESCRIPTION("qcom subpmic driver");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
