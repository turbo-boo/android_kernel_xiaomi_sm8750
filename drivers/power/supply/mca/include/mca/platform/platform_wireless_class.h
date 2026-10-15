#ifndef _MCA_PLATFORM_PLATFORM_WIRELESS_CLASS_H_
#define _MCA_PLATFORM_PLATFORM_WIRELESS_CLASS_H_
#include <linux/types.h>
#include <mca/protocol/protocol_class.h>

typedef enum {
	WLS_DEBUG_SET_FOD_NONE = 0,
	WLS_DEBUG_SET_FOD_ALL_DIRECTLY,
	WLS_DEBUG_SET_FOD_EPP_ALL,
	WLS_DEBUG_SET_FOD_EPP_ONE,
} WLS_DEBUG_SET_FOD_TYPE;

enum mca_wls_debug_set_fod_cmd {
	DEBUG_SET_ALL_FOD = 0,
	DEBUG_SET_ALL_EPP_FOD,
	DEBUG_SET_ONE_EPP_FOD,
	DEBUG_SET_FCC,
	DEBUG_SET_ICL,
	DEBUG_SET_VOUT,
	DEBUG_SET_FOD_TYPE,
	DEBUG_SET_FOD_NONE,
};

enum mca_wireless_role {
	WIRELESS_ROLE_MASTER = 0,
	WIRELESS_ROLE_SLAVE,
	WIRELESS_ROLE_MAX,
};

enum wls_chip_vendor {
	WLS_CHIP_VENDOR_FUDA1651 = 0,
	WLS_CHIP_VENDOR_FUDA1661,
	WLS_CHIP_VENDOR_FUDA1665,
	WLS_CHIP_VENDOR_SC9625,
	WLS_CHIP_VENDOR_SC96281,
};

struct platform_class_wireless_ops {
	int (*wls_enable_reverse_chg)(bool, void *data);
	int (*wls_is_present)(int *, void *data);
	int (*wls_set_vout)(int, void *data);
	int (*wls_get_vout)(int *, void *data);
	int (*wls_get_iout)(int *, void *data);
	int (*wls_get_vrect)(int *, void *data);
	int (*wls_get_tx_adapter)(int *, void *data);
	int (*wls_get_tx_adapter_by_i2c)(int *, void *data);
	int (*wls_get_fw_upgrade_fail_info)(char **, void *data);
	int (*wls_get_rsv_eppmode_fail)(int *, void *data);
	int (*wls_get_temp)(int *, void *data);
	int (*wls_set_enable_mode)(bool, void *data);
	int (*wls_is_car_adapter)(bool *, void *data);
	int (*wls_get_fw_version)(char *, void *data);
	int (*wls_get_rx_rtx_mode)(int *, void *data);
	int (*wls_set_fw_bin)(const char *, int, void *data);
	int (*wls_set_input_current_limit)(int, void *data);
	int (*wls_get_rx_int_flag)(int *, void *data);
	int (*wls_get_rx_power_mode)(u8 *, void *data);
	int (*wls_get_tx_max_power)(u8 *, void *data);
	int (*wls_get_auth_value)(int *, void *data);
	int (*wls_set_adapter_voltage)(int, void *data);
	int (*wls_get_tx_uuid)(u8 *, void *data);
	int (*wls_set_fod_params)(int, void *data);
	int (*wls_get_rx_fastcharge_status)(u8 *, void *data);
	int (*wls_receive_transparent_data)(u8 *, int, int *, void *data);
	int (*wls_send_transparent_data)(u8 *, u8, void *data);
	int (*wls_get_ss_voltage)(int *, void *data);
	int (*wls_do_renego)(u8, void *data);
	int (*wls_set_parallel_charge)(bool, void *data);
	int (*wls_get_vout_setted)(int *, void *data);
	int (*wls_get_poweroff_err_code)(u8 *, void *data);
	int (*wls_get_rx_err_code)(u8 *, void *data);
	int (*wls_get_tx_err_code)(u8 *, void *data);
	int (*wls_get_project_vendor)(int *, void *data);
	int (*wls_check_i2c_is_ok)(void *data);
	int (*wls_enable_rev_fod)(bool, void *data);
	int (*wls_send_tx_q_value)(u8, void *data);
	int (*wls_set_tx_fan_speed)(int, void *data);
	int (*wls_get_tx_fan_speed)(int *, void *data);
	int (*wls_set_rx_offset)(int, void *data);
	int (*wls_get_rx_offset)(int *, void *data);
	int (*wls_set_rx_sleep_mode)(int, void *data);
	int (*wls_download_fw_from_bin)(void *data);
	int (*wls_erase_fw)(void *data);
	int (*wls_get_fw_version_check)(u8 *, void *data);
	int (*wls_download_fw)(void *data);
	int (*wls_set_confirm_data)(void *data, u8);
	int (*wls_receive_test_cmd)(u8 *, int *, void *data);
	int (*wls_process_factory_cmd)(u8, void *data);
	int (*wls_get_hall_gpio_status)(bool *, void *data);
	int (*wls_get_magnetic_case_flag)(bool *, void *data);
	int (*wls_check_firmware_state)(bool *, void *data);
	int (*wls_set_debug_fod)(int *, int, void *data);
	int (*wls_get_debug_fod_type)(WLS_DEBUG_SET_FOD_TYPE *, void *data);
	int (*wls_set_debug_fod_params)(void *data);
	int (*wls_enable_vsys_ctrl)(bool, void *data);
	int (*wls_get_trx_isense)(int *, void *data);
	int (*wls_get_trx_vrect)(int *, void *data);
	int (*wls_set_external_boost_enable)(bool, void *data);
	int (*wls_get_phone_case_category)(int *, void *data);
	int (*wls_set_phone_case_category)(int, void *data);
	int (*wls_switch_bridge)(bool, void *data);
	int (*wls_get_brg_rect_mode)(u8 *, void *data);
	int (*wls_get_pen_hall3)(int *, void *data);
	int (*wls_get_pen_hall3_s)(int *, void *data);
	int (*wls_get_pen_hall4)(int *, void *data);
	int (*wls_get_pen_hall4_s)(int *, void *data);
	int (*wls_get_pen_hall_ppe_n)(int *, void *data);
	int (*wls_get_pen_hall_ppe_s)(int *, void *data);
	int (*wls_get_pen_full_flag)(bool *, void *data);
	int (*wls_get_pen_place_err)(int *, void *data);
	int (*wls_set_pen_place_err)(int, void *data);
	int (*wls_get_pen_mac)(u8 *, void *data);
	int (*wls_get_pen_soc)(int *, void *data);
	int (*wls_get_reverse_chg_en)(bool *, void *data);
	int (*wls_set_hboost_enable)(bool, void *data);
	int (*wls_set_charge_type)(int, void *data);
	int (*wls_get_tx_iout)(int *, void *data);
	int (*wls_get_tx_vout)(int *, void *data);
	int (*wls_get_rx_brg_status)(int *, void *data);
};

int platform_class_wireless_register_ops(unsigned int role, void *data,
					 struct platform_class_wireless_ops *ops);
int platform_class_wireless_enable_reverse_chg(unsigned int role, bool enable);
int platform_class_wireless_is_present(unsigned int role, int *present);
int platform_class_wireless_set_vout(unsigned int role, int vout);
int platform_class_wireless_get_vout(unsigned int role, int *vout);
int platform_class_wireless_get_iout(unsigned int role, int *iout);
int platform_class_wireless_get_vrect(unsigned int role, int *vrect);
int platform_class_wireless_get_tx_adapter(unsigned int role, int *adapter);
int platform_class_wireless_get_tx_adapter_by_i2c(unsigned int role, int *adapter);
int platform_class_wireless_get_fw_upgrade_fail_info(unsigned int role, char **info);
int platform_class_wireless_get_rsv_eppmode_fail(unsigned int role, int *fail);
int platform_class_wireless_get_temp(unsigned int role, int *temp);
int platform_class_wireless_set_enable_mode(unsigned int role, bool enable);
int platform_class_wireless_is_car_adapter(unsigned int role, bool *enable);
int platform_class_wireless_get_fw_version(unsigned int role, char *buf);
int platform_class_wireless_get_rx_rtx_mode(unsigned int role, int *mode);
int platform_class_wireless_set_fw_bin(unsigned int role, const char *buf, int count);
int platform_class_wireless_set_input_current_limit(unsigned int role, int value);
int platform_class_wireless_get_rx_int_flag(unsigned int role, int *int_flag);
int platform_class_wireless_get_rx_power_mode(unsigned int role, u8 *power_mode);
int platform_class_wireless_get_tx_max_power(unsigned int role, u8 *max_power);
int platform_class_wireless_get_auth_value(unsigned int role, int *value);
int platform_class_wireless_set_adapter_voltage(unsigned int role, int voltage);
int platform_class_wireless_get_tx_uuid(unsigned int role, u8 *uuid);
int platform_class_wireless_set_fod_params(unsigned int role, int value);
int platform_class_wireless_get_rx_fastcharge_status(unsigned int role, u8 *fc_flag);
int platform_class_wireless_receive_transparent_data(unsigned int role, u8 *rcv_value,
						     int buff_len, int *rcv_len);
int platform_class_wireless_send_transparent_data(unsigned int role, u8 *send_data,
						  u8 length);
int platform_class_wireless_get_ss_voltage(unsigned int role, int *ss_voltage);
int platform_class_wireless_do_renego(unsigned int role, u8 max_power);
int platform_class_wireless_set_parallel_charge(unsigned int role, bool parallel);
int platform_class_wireless_get_vout_setted(unsigned int role, int *vout_setted);
int platform_class_wireless_get_poweroff_err_code(unsigned int role, u8 *err_code);
int platform_class_wireless_get_rx_err_code(unsigned int role, u8 *err_code);
int platform_class_wireless_get_tx_err_code(unsigned int role, u8 *err_code);
int platform_class_wireless_get_project_vendor(unsigned int role, int *project_vendor);
int platform_class_wireless_check_i2c_is_ok(unsigned int role);
int platform_class_wireless_enable_rev_fod(unsigned int role, bool enable);
int platform_class_wireless_send_tx_q_value(unsigned int role, u8 value);
int platform_class_wireless_set_tx_fan_speed(unsigned int role, int value);
int platform_class_wireless_get_tx_fan_speed(unsigned int role, int *value);
int platform_class_wireless_set_rx_offset(unsigned int role, int rx_offset);
int platform_class_wireless_get_rx_offset(unsigned int role, int *rx_offset);
int platform_class_wireless_set_rx_sleep_mode(unsigned int role, int sleep_for_dam);
int platform_class_wireless_download_fw_from_bin(unsigned int role);
int platform_class_wireless_erase_fw(unsigned int role);
int platform_class_wireless_get_fw_version_check(unsigned int role, u8 *check_result);
int platform_class_wireless_download_fw(unsigned int role);
int platform_class_wireless_set_confirm_data(unsigned int role, u8 confirm_data);
int platform_class_wireless_receive_test_cmd(unsigned int role, u8 *rev_data, int *length);
int platform_class_wireless_process_factory_cmd(unsigned int role, u8 cmd);
int platform_class_wireless_get_hall_gpio_status(unsigned int role, bool *status);
int platform_class_wireless_get_magnetic_case_flag(unsigned int role, bool *status);
int platform_class_wireless_check_firmware_state(unsigned int role, bool *update);
int platform_class_wireless_set_debug_fod(unsigned int role, int *args, int count);
int platform_class_wireless_get_debug_fod_type(unsigned int role,
					       WLS_DEBUG_SET_FOD_TYPE *type);
int platform_class_wireless_set_debug_fod_params(unsigned int role);
int platform_class_wireless_enable_vsys_ctrl(unsigned int role, bool enable);
int platform_class_wireless_get_trx_isense(unsigned int role, int *isense);
int platform_class_wireless_get_trx_vrect(unsigned int role, int *vrect);
int platform_class_wireless_get_phone_case_category(unsigned int role, int *category);
int platform_class_wireless_set_phone_case_category(unsigned int role, int category);
int platform_class_wireless_switch_bridge(unsigned int role, bool full);
int platform_class_wireless_get_pen_hall3(unsigned int role, int *val);
int platform_class_wireless_get_pen_hall3_s(unsigned int role, int *val);
int platform_class_wireless_get_pen_hall4(unsigned int role, int *val);
int platform_class_wireless_get_pen_hall4_s(unsigned int role, int *val);
int platform_class_wireless_get_pen_hall_ppe_n(unsigned int role, int *val);
int platform_class_wireless_get_pen_hall_ppe_s(unsigned int role, int *val);
int platform_class_wireless_get_pen_full_flag(unsigned int role, bool *flag);
int platform_class_wireless_get_pen_place_err(unsigned int role, int *err);
int platform_class_wireless_set_pen_place_err(unsigned int role, int err);
int platform_class_wireless_get_pen_mac(unsigned int role, u8 *mac);
int platform_class_wireless_get_pen_soc(unsigned int role, int *soc);
int platform_class_wireless_get_reverse_chg_en(unsigned int role, bool *en);
int platform_class_wireless_set_hboost_enable(unsigned int role, bool en);
int platform_class_wireless_set_charge_type(unsigned int role, int type);
int platform_class_wireless_set_external_boost_enable(unsigned int role, bool en);
int platform_class_wireless_get_tx_iout(unsigned int role, int *iout);
int platform_class_wireless_get_tx_vout(unsigned int role, int *vout);
int platform_class_wireless_get_rx_brg_status(unsigned int role, int *status);

void platform_class_wireless_unregister_ops(unsigned int role, void *data);

#endif
