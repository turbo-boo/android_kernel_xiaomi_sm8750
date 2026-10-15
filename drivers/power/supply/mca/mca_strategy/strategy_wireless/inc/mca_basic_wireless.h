/* SPDX-License-Identifier: GPL-2.0 */
/*
 *mca_basic_wireless.h
 *
 * mca basic wireless strategy driver
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

#ifndef __MCA_BASIC_WIRELESS_H__
#define __MCA_BASIC_WIRELESS_H__

#include <linux/wait.h>
#include <mca/strategy/strategy_wireless_class.h>

#define MCA_WLS_CHG_CURRENT_DEFAULT_VALUE 100
#define MCA_WLS_CHG_VTERM_DEFAULT_VALUE 4400
#define MCA_WLS_CHG_DEFAULT_TX_Q2 27
#define MCA_WLS_CHG_DEFAULT_TX_Q1 0x15
#define XIAOMI_TX_UUID_LOW_INDUCTANCE_50W 0x09010A01
#define MCA_WLS_CHG_TX_QLIMIT_FCC_5W 1000
#define MCA_WLS_CHG_TX_QLIMIT_ICL_5W 400
#define MCA_WLS_CHG_DEFAULT_DIV2_RX_MAX_IOUT 2650
#define MCA_WLS_CHG_DEFAULT_DIV4_RX_MAX_IOUT 2650
#define MCA_WLS_CHG_RX_MAX_TEMP 84
#define MCA_WLS_CHG_RX_MAX_TEMP_SC96281 90
#define MCA_WLS_CHG_RX_OTP_DELTA_TEMP 10
#define MCA_WLS_CHG_REDUCE_FCC_VALUE_50W_MA 8000
#define MCA_WLS_CHG_REDUCE_FCC_VALUE_80W_MA 9000
#define MCA_WLS_CHG_SUPER_CHG_POWER 30
#define MCA_WLS_CHG_FLASH_CHG_POWER 20
#define MCA_WLS_CHG_BATT_MISS_FCC 500
#define MCA_WLS_CHG_BATT_MISS_FV 4200
#define MCA_WLS_SUPPORT_MULTI_BUCK 0
#define MCA_WLS_CHARGE_INPUT_SUSPEND 0
#define MCA_WLS_CHG_VTERM_LOW_TH 4300

#define MCA_WLS_CHG_ENABLE_FASTCHG_CNT 2

#define MCA_WLS_CHG_MONITOR_WORK_NORMAL_INTERVAL 10000
#define MCA_WLS_CHG_MONITOR_WORK_FAST_INTERVAL 5000

#define TX_QVALUE_DELAY_MS 1000

//factory wpc test
#define FACTORY_TEST_CMD 0x1f
#define REVERSE_TEST_STOP_DELAY_MS 8000
#define FACTORY_TEST_CMD_REVERSE_REQ 0x30

//vout definition
#define BPP_DEFAULT_VOUT 6000
#define BPP_QC2_VOUT 6500
#define BPP_PLUS_VOUT 9000
#define EPP_DEFAULT_VOUT 11000
#define EPP_PLUS_VOUT 15000

#define EXTERNAL_ADAPTER_DEFAULT 14

#define FAST_CHARGE_RETRY_MAX_CNT 3
#define TRANS_FAIL_RETRY_MAX_CNT 3
#define START_WLS_LOOP_CHECK_CNT 3

#define SUPER_TX_VOUT_MIN_MV 20000
#define SUPER_TX_VOUT_MAX_MV 36000

#define SUPER_TX_FREQUENCY_MIN_KHZ 116
#define SUPER_TX_FREQUENCY_MAX_KHZ 141

#define UUID_PARA_MAX_GROUP 16

#define RX_CHECK_SUCCESS (1 << 0)
#define TX_CHECK_SUCCESS (1 << 1)
#define BOOT_CHECK_SUCCESS (1 << 2)
#define POWER_ON_UPDATE_TIMER (10 * 1000)

#define ALLOW_FFC_TEMP_LOW_THR 20
#define ALLOW_FFC_TEMP_HIGH_THR 48

//buckchg sw cv params
#define CHARGE_SW_CV_WORK_NORMAL_INTERVAL 5000
#define CHARGE_SW_CV_WORK_FAST_INTERVAL 2000
#define CHARGE_SW_CV_VBAT_ALARM_DELTA 10

#define MCA_WLS_CHARGE_DEFAULT_IBUS_CURRENT 500
#define MCA_WLS_CHARGE_DEFAULT_IBAT_CURRENT 500

// interrupts foward definition
enum mca_wireless_basic_rx_int_flag {
	RX_INT_UNKNOWN = 0,
	RX_INT_LDO_ON,
	RX_INT_FAST_CHARGE,
	RX_INT_AUTHEN_FINISH,
	RX_INT_RENEGO_DONE,
	RX_INT_ALARM_SUCCESS,
	RX_INT_ALARM_FAIL,
	RX_INT_OOB_GOOD,
	RX_INT_RPP,
	RX_INT_TRANSPARENT_SUCCESS,
	RX_INT_TRANSPARENT_FAIL,
	RX_INT_FACTORY_TEST,
	RX_INT_OCP_OTP_ALARM,
	RX_INT_FIRST_AUTHEN,
	RX_INT_POWER_OFF,
	RX_INT_POWER_ON,
	RX_INT_RENEGO_FAIL,
	RX_INT_ERR_CODE,
};

enum mca_wireless_basic_rx_iout_limit {
	MCA_BASIC_WLS_RX_IOUT_MAX_20W_2_1 = 2000,
	MCA_BASIC_WLS_RX_IOUT_MAX_30W_2_1 = 2800,
	MCA_BASIC_WLS_RX_IOUT_MAX_50W_2_1 = 4900,
	MCA_BASIC_WLS_RX_IOUT_MAX_20W_4_1 = 1100,
	MCA_BASIC_WLS_RX_IOUT_MAX_30W_4_1 = 1700,
	MCA_BASIC_WLS_RX_IOUT_MAX_50W_4_1 = 2500,
	MCA_BASIC_WLS_RX_IOUT_MAX_80W_4_1 = 3800,
	MCA_BASIC_WLS_RX_IOUT_MAX_INVALID = 1000,
};

enum mca_wireless_basic_uuid_para_ele {
	MCA_BASIC_WLS_UUID_NAME = 0,
	MCA_BASIC_WLS_UUID_PARA,
	MCA_BASIC_WLS_UUID_PARA_MAX,
};

#define ADAPTER_PARA_MAX_GROUP 16

enum mca_wireless_basic_compatible_para_ele {
	MCA_BASIC_WLS_STANDARD = 0,
	MCA_BASIC_WLS_MUSICAL,
	MCA_BASIC_WLS_PLATE,
	MCA_BASIC_WLS_CAR,
	MCA_BASIC_WLS_TRAIN,
	MCA_BASIC_WLS_REDMI_30W,
	MCA_BASIC_WLS_SAILBOAT,
	MCA_BASIC_WLS_LOWINDUCTANCE_50W,
	MCA_BASIC_WLS_LOWINDUCTANCE_80W,
	MCA_BASIC_WLS_SUPPORT_FAN,
	MCA_BASIC_WLS_MAGNET_30W_TX,
	MCA_BASIC_WLS_COMPATIBLE_PARA_MAX,
};

enum sys_op_mode {
	SYS_OP_MODE_AC_MISSING = 0,
	SYS_OP_MODE_BPP = 0x1,
	SYS_OP_MODE_EPP = 0x2,
	SYS_OP_MODE_PDDE = 0x4,
	SYS_OP_MODE_TX = 0x8,
	SYS_OP_MODE_TX_FOD = 0x9,
	SYS_OP_MODE_INVALID,
};

enum mca_wireless_basic_tx_q_adapter_type {
	ADAPTER_LOW_INDUCTANCE_TX_50W = 0,
	ADAPTER_LOW_INDUCTANCE_TX_80W,
	ADAPTER_LOW_INDUCTANCE_TX_100W,
	ADAPTER_TYPE_MAX,
};

enum auth_status {
	AUTH_STATUS_FAILED,
	AUTH_STATUS_SHAR1_OK,
	AUTH_STATUS_USB_TYPE_OK,
	AUTH_STATUS_UUID_OK = 4,
	AUTH_STATUS_TX_MAC_OK = 6,
};

enum batt_chgr_status {
	CHARGER_STATUS_INHIBIT,
	CHARGER_STATUS_TRICKLE,
	CHARGER_STATUS_PRECHARGE,
	CHARGER_STATUS_FULLON,
	CHARGER_STATUS_TAPER,
	CHARGER_STATUS_TERMINATION,
	CHARGER_STATUS_CHARGING_PAUSE,
	CHARGER_STATUS_CHARGING_DISABLED,
};

enum WLS_FORWARD_CHARGER_MODE {
	FORWARD_2_1_CHARGER_MODE = 2,
	FORWARD_4_1_CHARGER_MODE = 4,
};

enum mca_wireless_basic_chg_mode {
	CHG_MODE_DIV2 = 0,
	CHG_MODE_DIV4,
	CHG_MODE_MAX,
};

enum XM_WLS_CHGR_STAGE {
	WLS_NORMAL_MODE,
	WLS_TAPER_MODE,
	WLS_FULL_MODE,
	WLS_RECHG_MODE,
};

enum TX_CMD_TYPE {
	TX_CMD_TYPE_NONE,
	TX_CMD_TYPE_VOLTAGE,
	TX_CMD_TYPE_FREQUENCE,
	TX_CMD_TYPE_SECONDARY_REGULATOR,
};

enum ADAPTER_CMD_TYPE {
	ADAPTER_CMD_TYPE_F0 = 0XF0,
	ADAPTER_CMD_TYPE_F1 = 0XF1,
	ADAPTER_CMD_TYPE_F2 = 0XF2,
	ADAPTER_CMD_TYPE_F3 = 0XF3,
	ADAPTER_CMD_TYPE_F4 = 0XF4,
	ADAPTER_CMD_TYPE_F5 = 0XF5,
	ADAPTER_CMD_TYPE_MAX = 6,
};

enum mca_wireless_basic_adapter_cmd_type_setting_para {
	ADAPTER_CMD_TYPE_SETTING_ICL = 0,
	ADAPTER_CMD_TYPE_SETTING_FCC,
	ADAPTER_CMD_TYPE_SETTING_MAX,
};

enum mca_wireless_basic_adapter_cmd_type_para {
	ADAPTER_CMD_TYPE_FX = 0,
	ADAPTER_CMD_TYPE_FX_PARA,
	ADAPTER_CMD_TYPE_FX_MAX,
};

enum {
	POWER_SUPPLY_CHARGING_STATUS_UNKNOWN,
	POWER_SUPPLY_CHARGING_STATUS_CHARGING,
	POWER_SUPPLY_CHARGING_STATUS_DISCHARGING,
	POWER_SUPPLY_CHARGING_STATUS_NOT_CHARGING,
	POWER_SUPPLY_CHARGING_STATUS_FULL,
};

enum mca_basic_chg_attr_list {
	MCA_BASIC_CHG_WLS_DEBUG = 0,
	MCA_BASIC_CHG_WLS_CAR_ADAPTER,
	MCA_BASIC_CHG_WLS_FC_FLAG,
	MCA_BASIC_CHG_WLS_LOW_INDUCTANCE_OFFSET,
	MCA_BASIC_CHG_WLS_SET_RX_SLEEP,
	MCA_BASIC_CHG_WLS_AUDIO_PHONE_STS,
};

struct wireless_compatible_info {
	int standard_tx;
	int musical_box;
	int plate_tx;
	int car_mounted;
	int train_tx;
	int redmi_30w_tx;
	int sailboat_tx;
	int low_inductance_50w_tx;
	int low_inductance_80w_tx;
	int support_fan;
	int magnet_30w_tx;
};

struct wls_node_state_info {
	int wls_car_adapter;
	int wls_fc_flag;
	int low_inductance_offset;
	int set_rx_sleep;
};

struct uuid_adapter_info {
	int uuid;
	int uuid_para_size;
	struct wireless_compatible_info compatible_info;
};

enum reverse_charge_state {
	REVERSE_STATE_OPEN,
	REVERSE_STATE_TIMEOUT,
	REVERSE_STATE_ENDTRANS,
	REVERSE_STATE_FORWARD,
	REVERSE_STATE_TRANSFER,
	REVERSE_STATE_WAITPING,
};

enum FW_UPDATE_CMD {
	FW_UPDATE_POWER_ON,
	FW_UPDATE_ERASE = 97,
	FW_UPDATE_USER,
	FW_UPDATE_CHECK,
	FW_UPDATE_FORCE,
	FW_UPDATE_FROM_BIN,
	FW_UPDATE_MAX,
};

struct adapter_cmd_type_fx {
	int icl_setting;
	int fcc_setting;
};

struct adapter_cmd_type_info {
	u8 receive_data;
	struct adapter_cmd_type_fx *cmd_fx;
};

typedef enum trans_data_flag {
	TRANS_DATA_FLAG_NONE = 0,
	TRANS_DATA_FLAG_SOC,
	TRANS_DATA_FLAG_QVALUE,
	TRANS_DATA_FLAG_FAN_SPEED,
	TRANS_DATA_FLAG_VOUT_RANGE,
	TRANS_DATA_FLAG_FREQUENCE,
} TRANS_DATA_FLAG;

struct trans_data_lis_node {
	struct list_head lnode;
	TRANS_DATA_FLAG data_flag;
	int value;
};

struct strategy_basic_wireless_proc_data {
	enum XM_WLS_CHGR_STAGE chgr_stage;
	enum TX_CMD_TYPE current_for_tx_cmd;
	enum ADAPTER_CMD_TYPE current_for_adapter_cmd;

	u8 epp;
	u8 tx_max_power;
	u8 uuid[4];
	u8 fc_flag;
	u8 set_fastcharge_vout_cnt;
	u8 renegociation_cnt;
	u8 start_wls_loop_cnt;
	u8 set_tx_voltage_cnt;
	bool qc_enable;
	bool high_soc_fcc_set;
	bool start_vout_check_flag;
	bool high_start_soc;
	bool dev_auth;
	bool first_drawload;
	bool boost_wireless_vdd;
	//tx_adapter
	bool is_2_1_mode;
	bool is_4_1_mode;
	int uuid_value;
	int auth_data;
	int int_flag;
	int adapter_type_first;
	int adapter_type;
	int pre_iwls;
	int pre_vout;
	int batt_soc;
	int ss_voltage;
	int target_vout;
	int target_iwls;
	int chgr_status;
	int tx_q;
	int forward_charger_mode;
	int qc_type;
	bool pre_fastchg;
	bool magnet_tx_flag;
	bool magnetic_case_flag;
	bool fc_done;
	int vbat_cell_mv;
	int ibat_ma;
	int tx_frequency;
	int tx_voltage;

	struct wls_adapter_power_cap wireless_power;
};

struct strategy_basic_wireless_smartchg_data {
	int delta_fv;
};

struct strategy_wireless_irq_node {
	struct list_head node;
	int int_mask;
};

struct strategy_wireless_dev {
	bool stopping;
	struct device *dev;
	int strategy_init_done;
	struct power_supply *batt_psy;

	struct list_head header;
	wait_queue_head_t wait_que;
	spinlock_t list_lock;

	//voter
	struct mca_votable *input_limit_voter; //ICL
	struct mca_votable *charge_limit_voter; //FCC
	struct mca_votable *chg_enable_voter; //chg_enable
	struct mca_votable *vterm_voter;
	struct mca_votable *iterm_voter;
	struct mca_votable *bpp_in_voter;
	struct mca_votable *bppqc2_in_voter;
	struct mca_votable *bppqc3_in_voter;
	struct mca_votable *epp_in_voter;
	struct mca_votable *auth_20w_voter;
	struct mca_votable *auth_30w_voter;
	struct mca_votable *auth_50w_voter;
	struct mca_votable *auth_80w_voter;
	struct mca_votable *auth_voice_box_voter;
	struct mca_votable *auth_magnet_30w_voter;
	struct mca_votable *sw_qc_ichg_voter;
	struct mca_votable *sw_thermal_ichg_voter;
	struct mca_votable *wls_input_suspend_voter;
	struct mca_votable *input_suspend_voter;
	struct mca_votable *force_vout_6v_voter;

	//dt config
	int project_vendor;
	int cp_chip_vendor;
	int max_power;
	int support_mode;
	int soc_limit_fcc;
	int support_q_value;
	int support_q_tx[ADAPTER_TYPE_MAX];
	int support_multi_buck;
	bool support_base_flip;
	int wls_vdd_src;
	int ffc_temp_low;
	int ffc_temp_high;
	int support_hall;
	int pmic_fv_compensation;
	u8 tx_q1[ADAPTER_TYPE_MAX];
	int tx_q2[ADAPTER_TYPE_MAX];
	int rx_max_iout[CHG_MODE_MAX];

	struct adapter_cmd_type_info cmd_type_info[ADAPTER_CMD_TYPE_MAX];
	struct uuid_adapter_info uuid_adapter_info[UUID_PARA_MAX_GROUP];
	struct wls_node_state_info wls_node;

	//delay_work
	struct delayed_work trans_data_work;
	struct delayed_work monitor_work;
	struct delayed_work find_voter_work;
	struct delayed_work report_soc_decimal_work;
	struct delayed_work renegociation_work;
	struct delayed_work wireless_loop_work;
	struct delayed_work rx_fastcharge_work;
	struct delayed_work delay_report_status_work;
	struct delayed_work wls_drawload_work;
	struct delayed_work rx_alarm_work;
	struct delayed_work max_power_control_work;
	struct delayed_work wls_fw_state_work;
	struct delayed_work set_vdd_flag_work;
	struct delayed_work soc_limit_stepper_work;
	struct delayed_work update_wireless_thermal_work;
	struct delayed_work mutex_unlock_work;
	struct delayed_work sw_cv_work;
	struct delayed_work process_irq_work;
	struct delayed_work change_cp_mode_work;
	struct delayed_work get_adapter_work;
	struct delayed_work base_flip_sw_cv_work;

	bool wl_irq_running;
	bool changing_cp_mode;
	bool switching_bridge;
	int base_flip_ov_cnt;
	int irq_node_cnt;

	int online;
	int otg_boost_src;
	int soc_limit;

	int sw_qc_ichg;
	int sw_thermal_ichg;
	int debug_qc_ichg;
	int force_vout_6v;
	struct strategy_basic_wireless_proc_data proc_data;
	struct strategy_basic_wireless_smartchg_data smartchg_data;

	bool wait_for_reverse_test;
	int rx_temp;
	int quiet_sts;
	bool mutex_lock_sts;
	int audio_phone_sts;
	int phone_icl;
	int offstd_phone_icl;
	int phone_vol;
	bool sw_cv_running;
	int thermal_phone_flag;
	int batt_overhot;
	int batt_cold;
	int head_cnt;
};

#endif /* __MCA_BASIC_WIRELESS_H__ */
