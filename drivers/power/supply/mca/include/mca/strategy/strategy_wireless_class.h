#ifndef _MCA_STRATEGY_STRATEGY_WIRELESS_CLASS_H_
#define _MCA_STRATEGY_STRATEGY_WIRELESS_CLASS_H_

#include <linux/types.h>

enum mca_wls_firmware_state {
	FIRMWARE_NO_UPDATE = 0,
	FIRMWARE_NEED_UPDATE,
	FIRMWARE_UPDATING,
	FIRMWARE_UPDATE_FINISH,
	FIRMWARE_UPDATE_ERROR,
};

enum xm_wls_charger_type {
	XM_WLS_CHARGER_TYPE_UNKNOWN = 0,
	XM_WLS_CHARGER_TYPE_BPP,
	XM_WLS_CHARGER_TYPE_EPP,
	XM_WLS_CHARGER_TYPE_HPP,
};

enum mca_ovpgate_check_type {
	WIRED_CHG_TYPE = 0,
	REVCHG_TYPE,
};

struct wls_adapter_power_cap {
	int max_fcc;
	int max_power;
};

int strategy_class_wireless_ops_get_adapter_power(
	struct wls_adapter_power_cap *adapter_power);
int strategy_class_wireless_ops_set_parallel_charge(bool parallel_charge_flag);
int strategy_class_wireless_ops_get_wls_type(int *wls_type);
int strategy_class_wireless_ops_get_adapter_charger_mode(int *cp_charger_mode);
void strategy_class_wireless_op_get_rx_iout_limit(int *rx_iout_limit_ma);

#define WLS_SSDEV_POWER_MAX_INVALID 0
#define WLS_SSDEV_POWER_MAX_20W     20
#define WLS_SSDEV_POWER_MAX_30W     30
#define WLS_SSDEV_POWER_MAX_50W     50
#define WLS_SSDEV_POWER_MAX_80W     80
#define ADAPTER_MAX                  8

int mca_wireless_rev_set_wired_chg_ok(bool ok);
int mca_wireless_rev_enable_reverse_charge(bool enable);
int mca_wireless_rev_set_firmware_state(int state);
int mca_wireless_rev_get_rev_boost_default(int *rev_boost_default);
int mca_wireless_rev_get_reverse_chg(bool *reverse_chg_en);
int mca_wireless_rev_get_reverse_chg_state(int *state);
int mca_wireless_rev_get_user_reverse_chg(bool *user_reverse_chg);
int mca_wireless_rev_set_user_reverse_chg(bool user_reverse_chg);
int mca_wireless_rev_update_fw_version(int cmd);
int mca_wireless_rev_set_usb_plugin(bool wls_sleep_usb_insert);
int mca_wireless_rev_get_fw_update(bool *fw_update);

void strategy_wireless_enable_cp_error_irq(unsigned int enable);

#endif /* _MCA_STRATEGY_STRATEGY_WIRELESS_CLASS_H_ */
