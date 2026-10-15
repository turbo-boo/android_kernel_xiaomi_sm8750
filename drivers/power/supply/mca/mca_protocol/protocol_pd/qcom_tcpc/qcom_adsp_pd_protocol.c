// SPDX-License-Identifier: GPL-2.0
/* Xiaomi MCA ADSP-backed PD/PPS protocol for Dada. */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/swab.h>
#include <mca/common/mca_adsp_glink.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "qcom_adsp_pd"
#endif

enum adsp_pd_prop {
	PD_PROP_USB_REAL_TYPE = 0x2000c,
	PD_PROP_VERIFY_PROCESS = 0x21002,
	PD_PROP_VDM_VERSION = 0x21003,
	PD_PROP_VDM_VOLTAGE = 0x21004,
	PD_PROP_VDM_TEMP = 0x21005,
	PD_PROP_VDM_SESSION_SEED = 0x21006,
	PD_PROP_VDM_AUTH = 0x21007,
	PD_PROP_VDM_VERIFIED = 0x21008,
	PD_PROP_VDM_REMOVE_COMP = 0x21009,
	PD_PROP_VDM_REVERSE_AUTH = 0x2100a,
	PD_PROP_VDM_NAK = 0x2100b,
	PD_PROP_DATA_ROLE = 0x2100c,
	PD_PROP_CURRENT_STATE = 0x2100d,
	PD_PROP_ADAPTER_ID = 0x2100e,
	PD_PROP_ADAPTER_SVID = 0x2100f,
	PD_PROP_PD_VERIFIED = 0x21010,
	PD_PROP_PDOS = 0x21011,
	PD_PROP_VDM_STATE = 0x21012,
	PD_PROP_PPS_MAX_CUR = 0x21013,
	PD_PROP_PPS_APDO_MAX = 0x21014,
	PD_PROP_TYPEC_MODE = 0x21015,
	PD_PROP_TYPEC_CC_ORIENT = 0x21016,
	PD_PROP_SELECT_PPS_PDO = 0x21017,
	PD_PROP_FIXED_PD_VOLT = 0x21018,
	PD_PROP_PPS_STATUS = 0x21019,
	PD_PROP_PPS_MAX_POWER = 0x2101b,
	PD_PROP_HAS_DP = 0x2101e,
	PD_PROP_CID_STATUS = 0x2101f,
	PD_PROP_OTG_PLUGIN = 0x200d,
	PD_PROP_CC_TOGGLE = 0x21020,
	PD_PROP_SNK_SRC_MODE = 0x21021,
	PD_PROP_CC_STATUS = 0x21022,
	PD_PROP_CC_SHORT_VBUS = 0x21023,
	PD_PROP_PPS_PTF = 0x21024,
	PD_PROP_SUSPEND_SUPPORT = 0x21025,
	PD_PROP_ZIMI_CYPRESS = 0x21026,
};

#define ADSP_REAL_TYPE_PD 0x0b
#define ADSP_PD_TYPE_PD_VERIFY 0x0b
#define ADSP_PD_TYPE_PPS 0x0c
#define ADSP_VDM_REQ_INIT 200
#define ADSP_VDM_REQ_NAK 0xff

struct adsp_pd_protocol {
	struct device *dev;
	int pd_active;
	int pd_verified;
	struct mca_adsp_glink_ops glink_ops;
	struct notifier_block event_nb;
	struct notifier_block abnormal_nb;
};

static int adsp_read(int prop, void *value, size_t size)
{
	return mca_adsp_glink_read_prop(prop, value, size);
}

static int adsp_write(int prop, void *value, size_t size)
{
	return mca_adsp_glink_write_prop(prop, value, size);
}

static int adsp_pd_get_pps_max_power(unsigned int *power, void *data)
{
	/* Dada uses this legacy-named callback to send the power budget. */
	return adsp_write(PD_PROP_PPS_MAX_POWER, power, sizeof(*power));
}

static int adsp_pd_select_pps_pdo(int volt, int curr, void *data)
{
	u32 val = (curr / 50) | ((volt / 20) << 16);
	return adsp_write(PD_PROP_SELECT_PPS_PDO, &val, sizeof(val));
}

static int adsp_pd_get_pps_ptf(int *ptf, void *data)
{
	return adsp_read(PD_PROP_PPS_PTF, ptf, sizeof(*ptf));
}

static int adsp_pd_set_fixed_volt(int volt, void *data)
{
	/* Stock writes the voltage unconditionally; no verify-process gate. */
	return adsp_write(PD_PROP_FIXED_PD_VOLT, &volt, sizeof(volt));
}


static int adsp_pd_get_pps_max_cur(unsigned int *curr, void *data)
{
	return adsp_read(PD_PROP_PPS_MAX_CUR, curr, sizeof(*curr));
}

static int adsp_pd_get_pps_status(int *volt, int *curr, void *data)
{
	u32 val = 0;
	int ret = adsp_read(PD_PROP_PPS_STATUS, &val, sizeof(val));

	if (!ret) {
		*curr = (val & 0xffff) * 50;
		*volt = (val >> 16) * 20;
	}
	return ret;
}

static int adsp_pd_set_active(int active, void *data)
{
	((struct adsp_pd_protocol *)data)->pd_active = active;
	return 0;
}

static int adsp_pd_get_active(int *active, void *data)
{
	*active = ((struct adsp_pd_protocol *)data)->pd_active;
	return 0;
}

static int adsp_pd_get_apdo_max(unsigned int *apdo, void *data)
{
	return adsp_read(PD_PROP_PPS_APDO_MAX, apdo, sizeof(*apdo));
}

static int adsp_pd_get_type(int *type, void *data)
{
	struct adsp_pd_protocol *pd = data;
	int real_type = 0;
	int ret = adsp_read(PD_PROP_USB_REAL_TYPE, &real_type,
			    sizeof(real_type));

	if (ret)
		return ret;
	if (real_type != ADSP_REAL_TYPE_PD) {
		*type = XM_CHARGER_TYPE_UNKNOW;
		return 0;
	}
	*type = pd->pd_verified ? ADSP_PD_TYPE_PPS : ADSP_PD_TYPE_PD_VERIFY;
	return 0;
}

static int adsp_pd_get_typec_mode(int *mode, void *data)
{
	return adsp_read(PD_PROP_TYPEC_MODE, mode, sizeof(*mode));
}

static int adsp_pd_get_cc_orientation(int *orientation, void *data)
{
	return adsp_read(PD_PROP_TYPEC_CC_ORIENT, orientation,
			 sizeof(*orientation));
}

static int adsp_pd_get_adapter_id(unsigned int *id, void *data)
{
	return adsp_read(PD_PROP_ADAPTER_ID, id, sizeof(*id));
}

static int adsp_pd_get_adapter_svid(unsigned int *svid, void *data)
{
	return adsp_read(PD_PROP_ADAPTER_SVID, svid, sizeof(*svid));
}

static int adsp_pd_get_has_dp(bool *has_dp, void *data)
{
	return adsp_read(PD_PROP_HAS_DP, has_dp, sizeof(*has_dp));
}

static int adsp_pd_get_data_role(unsigned char *role, void *data)
{
	u32 val = 0;
	int ret = adsp_read(PD_PROP_DATA_ROLE, &val, sizeof(val));
	if (!ret)
		*role = val;
	return ret;
}

static int adsp_pd_get_current_state(char *state, int len, void *data)
{
	int raw = 0;
	int ret;

	if (!state || len <= 0)
		return -EINVAL;
	ret = adsp_read(PD_PROP_CURRENT_STATE, &raw, sizeof(raw));
	if (ret)
		return ret;
	switch (raw) {
	case 0:
		strscpy(state, "SRC_Ready", len);
		break;
	case 1:
		strscpy(state, "SNK_STARTUP", len);
		break;
	case 2:
		strscpy(state, "SNK_Ready", len);
		break;
	default:
		strscpy(state, "UNKNOWN", len);
		break;
	}
	return 0;
}

static int adsp_pd_get_pdos(struct pd_pdo *pdos, int count, void *data)
{
	if (count <= 0 || count > PROTOCOL_PD_MAX_PDO_NUMS)
		return -EINVAL;
	return adsp_read(PD_PROP_PDOS, pdos, count * sizeof(*pdos));
}

static int adsp_pd_set_verify_process(int verify, void *data)
{
	return adsp_write(PD_PROP_VERIFY_PROCESS, &verify, sizeof(verify));
}

static int adsp_pd_get_verify_process(int *verify, void *data)
{
	return adsp_read(PD_PROP_VERIFY_PROCESS, verify, sizeof(*verify));
}

static int adsp_pd_set_verified(int verified, void *data)
{
	struct adsp_pd_protocol *pd = data;
	pd->pd_verified = verified;
	return adsp_write(PD_PROP_PD_VERIFIED, &verified, sizeof(verified));
}

static int adsp_pd_get_verified(int *verified, void *data)
{
	*verified = ((struct adsp_pd_protocol *)data)->pd_verified;
	return 0;
}

static int adsp_pd_get_cid(bool *status, void *data)
{
	return adsp_read(PD_PROP_CID_STATUS, status, sizeof(*status));
}

static int adsp_pd_get_otg(bool *status, void *data)
{
	return adsp_read(PD_PROP_OTG_PLUGIN, status, sizeof(*status));
}

static int adsp_pd_set_cc_toggle(bool en, void *data)
{
	u8 val = en;
	return adsp_write(PD_PROP_CC_TOGGLE, &val, sizeof(val));
}

static int adsp_pd_get_cc_toggle(bool *en, void *data)
{
	return adsp_read(PD_PROP_CC_TOGGLE, en, sizeof(*en));
}

static int adsp_pd_get_snk_src_mode(int *mode, void *data)
{
	return adsp_read(PD_PROP_SNK_SRC_MODE, mode, sizeof(*mode));
}

static int adsp_pd_get_cc_status(bool *status, void *data)
{
	return adsp_read(PD_PROP_CC_STATUS, status, sizeof(*status));
}

static int adsp_pd_get_cc_short_vbus(int *status, void *data)
{
	return adsp_read(PD_PROP_CC_SHORT_VBUS, status, sizeof(*status));
}

static int adsp_pd_get_suspend_support(bool *supported, void *data)
{
	return adsp_read(PD_PROP_SUSPEND_SUPPORT, supported, sizeof(*supported));
}

static int adsp_pd_get_zimi_cypress(int *flag, void *data)
{
	return adsp_read(PD_PROP_ZIMI_CYPRESS, flag, sizeof(*flag));
}

static void adsp_pd_bswap4(u32 *d)
{
	d[0] = swab32(d[0]);
	d[1] = swab32(d[1]);
	d[2] = swab32(d[2]);
	d[3] = swab32(d[3]);
}

static int adsp_pd_request_vdm(enum uvdm_state cmd, unsigned int *vdm,
			       unsigned int len, void *data)
{
	int prop;

	/* Stock validates only the data pointer; len 0 is a valid VSafe0V write. */
	if (!vdm)
		return -EINVAL;
	switch (cmd) {
	case USBPD_UVDM_CHARGER_VERSION:
		prop = PD_PROP_VDM_VERSION;
		break;
	case USBPD_UVDM_CHARGER_VOLTAGE:
		prop = PD_PROP_VDM_VOLTAGE;
		break;
	case USBPD_UVDM_CHARGER_TEMP:
		prop = PD_PROP_VDM_TEMP;
		break;
	case USBPD_UVDM_SESSION_SEED:
		if (len < 4)
			return -EINVAL;
		adsp_pd_bswap4(vdm);
		prop = PD_PROP_VDM_SESSION_SEED;
		break;
	case USBPD_UVDM_AUTHENTICATION:
		if (len < 4)
			return -EINVAL;
		adsp_pd_bswap4(vdm);
		prop = PD_PROP_VDM_AUTH;
		break;
	case USBPD_UVDM_VERIFIED:
		prop = PD_PROP_VDM_VERIFIED;
		break;
	case USBPD_UVDM_REMOVE_COMPENSATION:
		prop = PD_PROP_VDM_REMOVE_COMP;
		break;
	case USBPD_UVDM_REVERSE_AUTHEN:
		if (len < 4)
			return -EINVAL;
		adsp_pd_bswap4(vdm);
		prop = PD_PROP_VDM_REVERSE_AUTH;
		break;
	default:
		if (cmd == ADSP_VDM_REQ_INIT)
			prop = PD_PROP_DATA_ROLE;
		else if (cmd == ADSP_VDM_REQ_NAK)
			prop = PD_PROP_VDM_NAK;
		else
			return -EOPNOTSUPP;
	}
	return adsp_write(prop, vdm, len);
}

static int adsp_pd_get_vdm(int *cmd, struct usbpd_vdm_data *vdm, void *data)
{
	int ret;
	u32 buf[4] = { 0 };

	ret = adsp_read(PD_PROP_VDM_STATE, cmd, sizeof(*cmd));
	if (ret)
		return ret;
	switch (*cmd) {
	case USBPD_UVDM_CHARGER_VERSION:
		return adsp_read(PD_PROP_VDM_VERSION, &vdm->ta_version,
				 sizeof(vdm->ta_version));
	case USBPD_UVDM_CHARGER_VOLTAGE:
		return adsp_read(PD_PROP_VDM_VOLTAGE, &vdm->ta_voltage,
				 sizeof(vdm->ta_voltage));
	case USBPD_UVDM_CHARGER_TEMP:
		return adsp_read(PD_PROP_VDM_TEMP, &vdm->ta_temp,
				 sizeof(vdm->ta_temp));
	case USBPD_UVDM_AUTHENTICATION:
		ret = adsp_read(PD_PROP_VDM_AUTH, buf, sizeof(buf));
		if (!ret) {
			vdm->s_secert[4] = buf[0];
			vdm->s_secert[5] = buf[1];
			vdm->s_secert[6] = buf[2];
			vdm->s_secert[7] = buf[3];
		}
		return ret;
	default:
		return 0;
	}
}

static struct protocol_class_pd_ops adsp_pd_ops = {
	.protocol_pd_pps_get_max_power = adsp_pd_get_pps_max_power,
	.protocol_pd_pps_pdo_select = adsp_pd_select_pps_pdo,
	.protocol_pd_get_pps_ptf = adsp_pd_get_pps_ptf,
	.protocol_pd_fixed_pdo_set_vol = adsp_pd_set_fixed_volt,
	.protocol_pd_get_pps_max_cur = adsp_pd_get_pps_max_cur,
	.protocol_pd_get_pps_status = adsp_pd_get_pps_status,
	.protocol_pd_set_pd_active = adsp_pd_set_active,
	.protocol_pd_get_pd_active = adsp_pd_get_active,
	.protocol_pd_get_pps_apdo_max = adsp_pd_get_apdo_max,
	.protocol_pd_get_pd_type = adsp_pd_get_type,
	.protocol_pd_get_typec_mode = adsp_pd_get_typec_mode,
	.protocol_pd_get_typec_cc_orientation = adsp_pd_get_cc_orientation,
	.protocol_pd_get_adapter_id = adsp_pd_get_adapter_id,
	.protocol_pd_get_adapter_svid = adsp_pd_get_adapter_svid,
	.protocol_pd_request_vdm_cmd = adsp_pd_request_vdm,
	.protocol_pd_get_vdm_cmd = adsp_pd_get_vdm,
	.protocol_pd_get_data_role = adsp_pd_get_data_role,
	.protocol_pd_get_current_state = adsp_pd_get_current_state,
	.protocol_pd_get_pdos = adsp_pd_get_pdos,
	.protocol_pd_set_verify_process = adsp_pd_set_verify_process,
	.protocol_pd_get_verify_process = adsp_pd_get_verify_process,
	.protocol_pd_set_pd_verifed = adsp_pd_set_verified,
	.protocol_pd_get_pd_verifed = adsp_pd_get_verified,
	.protocol_pd_get_has_dp = adsp_pd_get_has_dp,
	.protocol_pd_get_cid_status = adsp_pd_get_cid,
	.protocol_pd_get_otg_plugin_status = adsp_pd_get_otg,
	.protocol_pd_set_cc_toggle = adsp_pd_set_cc_toggle,
	.protocol_pd_get_cc_toggle = adsp_pd_get_cc_toggle,
	.protocol_pd_get_snk_src_mode = adsp_pd_get_snk_src_mode,
	.protocol_pd_get_cc_status = adsp_pd_get_cc_status,
	.protocol_pd_get_cc_short_vbus = adsp_pd_get_cc_short_vbus,
	.protocol_pd_get_suspend_support_status = adsp_pd_get_suspend_support,
	.protocol_pd_get_zimi_cypress_flag = adsp_pd_get_zimi_cypress,
};

static void adsp_pd_glink_down(void *priv)
{
	struct adsp_pd_protocol *pd = priv;
	pd->pd_verified = 0;
	pd->pd_active = 0;
}

/* Stock clears the verified state on a disconnect charge-type event. */
static int adsp_pd_event_cb(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	struct adsp_pd_protocol *pd = container_of(nb, struct adsp_pd_protocol,
						   event_nb);

	if (!data)
		pd->pd_verified = 0;
	return NOTIFY_OK;
}

/* Stock clears the verified state on the charge-abnormal event. */
static int adsp_pd_abnormal_cb(struct notifier_block *nb, unsigned long event,
			       void *data)
{
	struct adsp_pd_protocol *pd = container_of(nb, struct adsp_pd_protocol,
						   abnormal_nb);

	if (event == MCA_EVENT_CHARGE_ABNORMAL)
		pd->pd_verified = 0;
	return NOTIFY_OK;
}

static int adsp_pd_probe(struct platform_device *pdev)
{
	struct adsp_pd_protocol *pd;
	int ret;

	pd = devm_kzalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;
	pd->dev = &pdev->dev;
	pd->glink_ops.glink_state_down = adsp_pd_glink_down;
	platform_set_drvdata(pdev, pd);
	pd->event_nb.notifier_call = adsp_pd_event_cb;
	pd->abnormal_nb.notifier_call = adsp_pd_abnormal_cb;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_CHARGER_CONNECT,
					     &pd->event_nb);
	if (ret)
		return ret;
	ret = mca_event_block_notify_register(MCA_EVENT_CHARGE_STATUS,
					     &pd->abnormal_nb);
	if (ret)
		goto unregister_event;

	ret = protocol_class_pd_register_ops(TYPEC_PORT_0, &adsp_pd_ops, pd);
	if (ret)
		goto unregister_abnormal;
	ret = mca_adsp_glink_resister_ops(&pd->glink_ops, pd);
	if (ret)
		goto unregister_protocol;
	mca_log_info("Dada ADSP PD transport registered\n");
	return 0;

unregister_protocol:
	protocol_class_pd_unregister_ops(TYPEC_PORT_0, pd);
unregister_abnormal:
	mca_event_block_notify_unregister(MCA_EVENT_CHARGE_STATUS,
					  &pd->abnormal_nb);
unregister_event:
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &pd->event_nb);
	return ret;
}

static int adsp_pd_remove(struct platform_device *pdev)
{
	struct adsp_pd_protocol *pd = platform_get_drvdata(pdev);

	mca_event_block_notify_unregister(MCA_EVENT_TYPE_CHARGER_CONNECT,
					  &pd->event_nb);
	mca_event_block_notify_unregister(MCA_EVENT_CHARGE_STATUS,
					  &pd->abnormal_nb);
	mca_adsp_glink_unregister_ops(pd);
	protocol_class_pd_unregister_ops(TYPEC_PORT_0, pd);
	return 0;
}

static const struct of_device_id adsp_pd_match[] = {
	{ .compatible = "mca,adsp_pd_protocol" },
	{},
};
MODULE_DEVICE_TABLE(of, adsp_pd_match);

static struct platform_driver adsp_pd_driver = {
	.driver = {
		.name = "adsp_pd_protocol",
		.of_match_table = adsp_pd_match,
	},
	.probe = adsp_pd_probe,
	.remove = adsp_pd_remove,
};
module_platform_driver(adsp_pd_driver);

MODULE_DESCRIPTION("Xiaomi Dada ADSP PD/PPS protocol transport");
MODULE_LICENSE("GPL v2");
