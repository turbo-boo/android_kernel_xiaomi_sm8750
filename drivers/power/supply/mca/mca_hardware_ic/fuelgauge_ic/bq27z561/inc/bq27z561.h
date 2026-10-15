/* SPDX-License-Identifier: GPL-2.0 */
/*
 * bq27z51.h
 *
 * fuelgauge reg
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
#ifndef __BQ27Z561_H
#define __BQ27Z561_H

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <asm/atomic.h>

enum print_reason {
	PR_INTERRUPT	= BIT(0),
	PR_REGISTER	 = BIT(1),
	PR_OEM		= BIT(2),
	PR_DEBUG	= BIT(3),
};

enum fg_cell_vendor {
	FG_CELL_NAME_A = 65,
	FG_CELL_NAME_B,
	FG_CELL_NAME_C,
	FG_CELL_NAME_D,
	FG_CELL_NAME_E,
	FG_CELL_NAME_F,
	FG_CELL_NAME_G,
	FG_CELL_NAME_H,
	FG_CELL_NAME_I,
	FG_CELL_NAME_J,
	FG_CELL_NAME_L = 76,
};


enum fg_pack_vendor {
	PACK_SUPPLIER_BYD = 0,
	PACK_SUPPLIER_COSLIGHT,
	PACK_SUPPLIER_SUNWODA,
	PACK_SUPPLIER_NVT,
	PACK_SUPPLIER_SCUD,
	PACK_SUPPLIER_TWS,
	PACK_SUPPLIER_LISHEN,
	PACK_SUPPLIER_DESAY,
};

#ifdef CONFIG_FACTORY_BUILD
#define PROBE_CNT_MAX	30
#else
#define PROBE_CNT_MAX	50
#endif

#define	INVALID_REG_ADDR	0xFF

#define FG_FLAGS_CO			BIT(2)
#define FG_FLAGS_FD				BIT(4)
#define	FG_FLAGS_FC				BIT(5)
#define	FG_FLAGS_DSG				BIT(6)
#define FG_FLAGS_RCA				BIT(9)
#define FG_FLAGS_FASTCHAGE			BIT(5)

#define BATTERY_DIGEST_LEN 32
#define BATTERY_RANDOM_LEN 64
#define BATTERY_UNSEAL_KEY_LEN 4

#define DEFUALT_FULL_DESIGN		5000

#define BQ_REPORT_FULL_SOC	9800
#define BQ_CHARGE_FULL_SOC	9750
#define BQ_RECHARGE_SOC		9800
#define BQ_DEFUALT_FULL_SOC		100

#define BATT_NORMAL_H_THRESHOLD		350

#define TI_TERM_CURRENT_L		1100
#define TI_TERM_CURRENT_H		1232

#define NFG_TERM_CURRENT_L		1237
#define NFG_TERM_CURRENT_H		1458

#define BQ27Z561_DEFUALT_TERM		-200
#define BQ27Z561_DEFUALT_FFC_TERM	-680
#define BQ27Z561_DEFUALT_RECHARGE_VOL	4380

#define NO_FFC_OVER_FV	(4464 * 1000)
#define STEP_FV	(16 * 1000)
#define BATT_WARM_THRESHOLD		480

#define PD_CHG_UPDATE_DELAY_US	20	/*20 sec*/
#define BQ_I2C_FAILED_SOC	-107
#define BQ_I2C_FAILED_TEMP	-307
#define DEBUG_CAPATICY		15
#define BMS_FG_VERIFY		"BMS_FG_VERIFY"
#define CC_CV_STEP		"CC_CV_STEP"
#define FCCMAIN_FG_CHARGE_FULL_VOTER		"FCCMAIN_FG_CHARGE_FULL_VOTER"
#define OVER_FV_VOTER	"OVER_FV_VOTER"
#define SMART_BATTERY_FV	"SMART_BATTERY_FV"

#define BQ_PACK_MAXIUM_VOLTAGE_FOR_PMIC			4490
#define BQ_MAXIUM_VOLTAGE_FOR_CELL			4480
#define BQ_PACK_MAXIUM_VOLTAGE_FOR_PMIC_SAFETY	4477

#define FG_ERROR_CHECK_MAX_TIMES 3
#define FG_ERROR_FAKE_TEMP 250
#define FG_FAKE_TEMP_NONE -999

enum bq_fg_reg_idx {
	BQ_FG_REG_CTRL = 0,
	BQ_FG_REG_TEMP,		/* Battery Temperature */
	BQ_FG_REG_VOLT,		/* Battery Voltage */
	BQ_FG_REG_CN,		/* Current Now */
	BQ_FG_REG_AVER_CURRENT,		/* Average Current */
	BQ_FG_REG_BATT_STATUS,	/* BatteryStatus */
	BQ_FG_REG_ORIGINAL_TEMP,	/* InternalTemperature (raw die temp) */
	BQ_FG_REG_TTE,		/* Time to Empty */
	BQ_FG_REG_TTF,		/* Time to Full */
	BQ_FG_REG_FCC,		/* Full Charge Capacity */
	BQ_FG_REG_RM,		/* Remaining Capacity */
	BQ_FG_REG_CC,		/* Cycle Count */
	BQ_FG_REG_SOC,		/* Relative State of Charge */
	BQ_FG_REG_SOH,		/* State of Health */
	BQ_FG_REG_CHG_VOL,	/* Charging Voltage*/
	BQ_FG_REG_CHG_CUR,	/* Charging Current*/
	BQ_FG_REG_DC,		/* Design Capacity */
	BQ_FG_REG_ALT_MAC,	/* AltManufactureAccess*/
	BQ_FG_REG_MAC_DATA,	/* MACData*/
	BQ_FG_REG_MAC_CHKSUM,	/* MACChecksum */
	BQ_FG_REG_MAC_DATA_LEN,	/* MACDataLen */
	NVT_FG_REG_OVER_PEAK,	/*over peak flag*/
	NVT_FG_REG_CUR_DEV,		/*current deviation*/
	NVT_FG_REG_POW_DEV,		/*power deviation*/
	NVT_FG_AVE_CUR,		/*10s average current*/
	NVT_FG_AVE_TEMP,		/*10s average tempature*/
	NVT_FG_REG_SOA_L,               /* soa alert */
	NVT_FG_REG_SOA_H,               /* soa alert */
	NVT_FG_REG_ISC,          /* isc alert */
	NVT_FG_REG_START_LEARNING,	/*start learning*/
	NVT_FG_REG_STOP_LEARNING,	/*stop learning*/
	NVT_FG_REG_EST_POWER,	/*Estimated power input*/
	NVT_FG_REG_ACT_POWER,	/*Actual power output*/
	NVT_FG_REG_POWER_DEV,	/*Estimated power-Actual power output*/
	NVT_FG_REG_TIME_DEV,	/*Estimated power time-Actual power time output*/
	NVT_FG_REG_CONST_POWER,	/*Constant power value*/
	NVT_FG_REG_CONST_POWER_TM,	/*Constant power value time*/
	NVT_FG_REG_REF_POWER,	/*Recommended reference power*/
	NVT_FG_REG_REF_CURRENT,	/*nvt recommended discharge current*/
	NVT_FG_REG_NVT_REF_CURRENT,	/*nvt recommended discharge power*/
	NVT_FG_REG_START_LEARNING_B,	/*start learning B*/
	NVT_FG_REG_STOP_LEARNING_B,	/*stop learning B*/
	NVT_FG_REG_EST_POWER_B,	/*Estimated power input B*/
	NVT_FG_REG_ACT_POWER_B,	/*Actual power output B*/
	NVT_FG_REG_POWER_DEV_B,	/*Estimated power-Actual power output B*/
	NUM_REGS,
};

static u8 bq27z561_regs[NUM_REGS] = {
	0x00,	/* CONTROL */
	0x06,	/* TEMP */
	0x08,	/* VOLT */
	0x0C,	/* CURRENT NOW */
	0x14,	/* AVG CURRENT */
	0x0A,	/* FLAGS */
	0x1E,	/* InternalTemperature (original/raw die temp) */
	0x16,	/* Time to empty */
	0x18,	/* Time to full */
	0x12,	/* Full charge capacity */
	0x10,	/* Remaining Capacity */
	0x2A,	/* CycleCount */
	0x2C,	/* State of Charge */
	0x2E,	/* State of Health */
	0x30,	/* Charging Voltage*/
	0x32,	/* Charging Current*/
	0x3C,	/* Design Capacity */
	0x3E,	/* AltManufacturerAccess*/
	0x40,	/* MACData*/
	0x60,	/* MACChecksum */
	0x61,	/* MACDataLen */
	0x78,	/*over peak flag*/
	0x79,	/*current deviation*/
	0x7B,	/*power deviation*/
	0x81,	/*10s average current*/
	0x83,	/*10s average tempature*/
	0x70,   /*soa Alert */
	0x71,   /*soa Alert */
	0x72,   /* isc Alert level */
	0x85,	/*start learning*/
	0x86,	/*stop learning*/
	0x87,	/*Estimated power input*/
	0x89,	/*Actual power output*/
	0x8B,	/*Estimated power-Actual power output*/
	0x8D,	/*Estimated power time-Actual power time output*/
	0xA1,	/*Constant power value*/
	0xA3,	/*Constant power value time*/
	0xA5,	/*Recommended reference power*/
	0xA7,	/*nvt recommended discharge current*/
	0xA9,	/*nvt recommended discharge power*/
	0x93,	/*start learning B*/
	0x94,	/*stop learning B*/
	0x95,	/*Estimated power input B*/
	0x97,	/*Actual power output B*/
	0x99,	/*Estimated power-Actual power output B*/
};

enum bq_fg_mac_cmd {
	FG_MAC_CMD_CTRL_STATUS	= 0x0000,
	FG_MAC_CMD_DEV_TYPE	= 0x0001,
	FG_MAC_CMD_FW_VER	= 0x0002,
	FG_MAC_CMD_HW_VER	= 0x0003,
	FG_MAC_CMD_IF_SIG	= 0x0004,
	FG_MCA_CMD_DF	=	0x0005,
	FG_MAC_CMD_CHEM_ID	= 0x0006,
	FG_MAC_CMD_GAUGING	= 0x0021,
	FG_MAC_CMD_SEAL		= 0x0030,
	FG_MAC_CMD_FASTCHARGE_EN = 0x003E,
	FG_MAC_CMD_FASTCHARGE_DIS = 0x003F,
	FG_MAC_CMD_DEV_RESET	= 0x0041,
	FG_MAC_CMD_DEVICE_NAME	= 0x004A,
	FG_MAC_CMD_CHEM_NAME	= 0x004B,
	FG_MAC_CMD_MANU_NAME	= 0x004C,
	FG_MAC_CMD_CUTOFF_VOLTAGE	= 0x0050,
	FG_MCA_CMD_SEAL_STATE	= 0x0054,
	FG_MAC_CMD_CHARGING_STATUS = 0x0055,
	FG_MAC_CMD_GAGUE_STATUS = 0x0056,
	FG_MAC_CMD_CHEM_DF_SIGN	= 0x0008,
	FG_MAC_CMD_CO_AUTO_OPEN	= 0x0057,
	FG_MAC_CMD_CUV_OCD	= 0x005E,
	FG_MAC_CMD_HCUV_HOCD	= 0x005F,
	FG_MAC_CMD_LIFETIME1	= 0x0060,
	FG_MAC_CMD_LIFETIME2	= 0x0061,
	FG_MAC_CMD_LIFETIME3	= 0x0062,
	FG_MAC_CMD_CALC_RVALUE = 0x0066,
	FG_MAC_CMD_OVER_VOL_DURATION = 0x0067,
	FG_MAC_CMD_REL_SOH = 0x0069,
	FG_MAC_CMD_BATT_SN = 0x0070,
	FG_MAC_CMD_QMAX_CYCLECOUNT	= 0x0071,
	FG_MAC_CMD_ITSTATUS1	= 0x0073,
	FG_MAC_CMD_QMAX		= 0x0075,
	FG_MAC_CMD_FCC_SOH	= 0x0077,
	FG_MAC_CMD_UI_SOH = 0x007B,
	FG_MAC_CMD_CELL_QMAX = 0x0080,
	FG_MAC_CMD_SREMC_SFCC = 0x0083,
	FG_MAC_CMD_EIS_SOH = 0x009D,
	FG_MAC_CMD_CO_CTRL = 0x00B2,
	FG_MAC_CMD_ENABLE_CO = 0x00C0,
	FG_MAC_CMD_CLOSE_CO = 0x00C9,
	FG_MAC_CMD_OPEN_CO = 0x00CC,
	FG_MAC_CMD_CALIBRATION_INFO_1 = 0x00D1,
	FG_MAC_CMD_CALIBRATION_INFO_2 = 0x00D2,
	FG_MAC_CMD_CALIBRATION_INFO_3 = 0x00D3,
	FG_MAC_CMD_CALIBRATION_INFO_4 = 0x00D4,
	FG_MAC_CMD_MIN_VOLTAGE	= 0x1031,
	FG_MAC_CMD_DCR_SLOPE	= 0x10A0,
	FG_MAC_CMD_CSD		= 0x10A2,
	FG_MAC_CMD_CLEAR_COUNT_DATA = 0x1054,
	FG_MAC_CMD_FORCE_SIM_SOH = 0x1056,
	FG_MAC_CMD_MIXDATARECORD1 = 0x2001,
	FG_MAC_CMD_RA_TABLE	= 0x40C0,
	FG_MAC_CMD_FEATURE_INFO = 0x40E0,
	FG_MAC_CMD_FW_RUNTIME	= 0x4441,
	FG_MAC_CMD_TERM_CURRENTS = 0x456E,
	FG_MAC_CMD_RECORD_VOLTAGE_LEVEL = 0x448A,
	FG_MAC_CMD_UPDATE_VERSION = 0x440C,
};

enum fg_seal_status {
	SEAL_STATE_RSVED,
	SEAL_STATE_FA,
	SEAL_STATE_UNSEALED,
	SEAL_STATE_SEALED,
};

enum bq_fg_device {
	BQ27Z561_MASTER = 0,
	BQ27Z561_SLAVE,
	BQ28Z610,
};

enum fg_device_role {
	FG_MASTER,
	FG_SLAVE,
	FG_THIRD,
	FG_MAX,
};

enum fg_fw_ver {
	FW_DEFAULT,
	FW_R0,
	FW_R1,
};

enum fg_cell_volt {
	CELL_VOLTAGE1,
	CELL_VOLTAGE2,
};

enum fg_cell_sremc_sfcc {
	CELL_SREMC1,
	CELL_SREMC2,
	CELL_SFCC1,
	CELL_SFCC2,
};

enum fg_cell_qmax {
	CELL_QMAX1,
	CELL_QMAX2,
};

#define BQ_ROLE_MASTER		0
#define BQ_ROLE_SLAVE		1

static int bq_mode_data[] = {
	[BQ27Z561_MASTER] = BQ_ROLE_MASTER,
	[BQ27Z561_SLAVE] = BQ_ROLE_SLAVE,
	[BQ28Z610] = BQ_ROLE_MASTER,
};

static const unsigned char *device2str[] = {
	"bq27z561_master",
	"bq27z561_slave",
	"bq27z561",
	"bq28z610",
};
struct cold_thermal {
	int index;
	int temp_l;
	int temp_h;
	int curr_th;
};

#define STEP_TABLE_MAX 3
struct step_config {
	int volt_lim;
	int curr_lim;
};

static struct step_config cc_cv_step_config[STEP_TABLE_MAX] = {
	{4200-3, 8820},
	{4300-3, 7350},
	{4400-3, 5880},
};

enum fg_vendor {
	BQ27Z561 = 0,  // TI
	NFG1000A,  // NVT-eastsoft
	NFG1000B,  // NVT-chipsea
	MPC8011B,
	MPC7012,
	MPC7021,
	BQ30Z55,
	BQ40Z50,
	BQ27Z746,
	TIBQ28Z610,
	MAX1789,
	RAA241200,
	SC58561,
	SC58511,
	SC58520,
	VENDOR_INVALID,
};

#define VOLTAGE_COL_MAX 6
#define VOLTAGE_ROW_MAX 1
struct bq_fg_chip {
	struct device *dev;
	struct device *sysfs_dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct device_node *regmap_node;
	struct platform_device *pdev;
	/* dts config */
	unsigned int dev_role;
	int batt_dc;
	int soh_th;
	int cycle_th;
	int cycle_step;
	int resistance_id;
	int *dec_rate_seq;
	int dec_rate_len;

	struct mutex i2c_rw_lock;
	struct mutex data_lock;
	atomic_t pm_suspend;
	atomic_t digest_in_process;
	int fw_ver;
	int df_ver;
	int nfg1000_section;
	int ota_update_flag;
	int nfg1000_ver;
	u32 nfg1000_section_marker;
	u32 nfg1000_magic;
	u8 nfg1000_ota_buf[512];
	u8 chip_ok;
	u8 regs[NUM_REGS];
	char device_name[8];
	char fake_device_name[8];
	char eeprom_version;

	/* status tracking */
	bool batt_fc;
	bool batt_fd;	/* full depleted */

	bool batt_dsg;
	bool batt_rca;	/* remaining capacity alarm */

	bool batt_fc_1;
	bool batt_fd_1;	/* full depleted */
	bool batt_tc_1;
	bool batt_td_1;	/* full depleted */
	bool support_voltage_record_level;
	bool support_version_compatible;
	bool support_co_by_gpio;
	int co_gpio;
	int support_nvt1000_ota;
	u8 version_number;
	u8 start_byte_address;
	u8 byte_length;
	u8 *record_voltage;

	int seal_state; /* 0 - Full Access, 1 - Unsealed, 2 - Sealed */
	int last_soc;
	int batt_id;
	int Qmax_old;
	int batt_temp;
	int original_temp;
	int batt_volt;
	int batt_curr;
	int batt_rm;
	int batt_fcc;
	int batt_cycle_count;

	/* debug */
	int skip_reads;
	int skip_writes;

	u8 digest[BATTERY_DIGEST_LEN];
	bool verify_digest_success;
	int batt_auth;
	int constant_charge_current_max;
	u8 key[BATTERY_UNSEAL_KEY_LEN];

	/* workaround for debug or other purpose */
	bool	ignore_digest_for_debug;
	bool	old_hw;
	bool fg_error;

	struct cold_thermal *cold_thermal_seq;
	int	cold_thermal_len;
	bool	fast_mode;

	int cell1_max;
	int max_charge_current;
	int max_discharge_current;
	int max_temp_cell;
	int min_temp_cell;
	int total_fw_runtime;
	int time_spent_in_lt;
	int time_spent_in_ht;
	int time_spent_in_ot;
	const char *cell_name;
	int fg_vendor;
	int pack_vendor;
	int fake_cycle;
	int fake_temp;
	int fake_isc;
	int adapt_power;
	int aged_flag;
	unsigned int ui_soh;
	int count_level1;
	int count_level2;
	int count_level3;
	int count_lowtemp;
	int fake_count_level1;
	int fake_count_level2;
	int fake_count_level3;
	int fake_count_lowtemp;
	int update_fw_status;
	int max_pow;
	int cuv_count;
	int cuv_last_cycle;
	int hcuv_count;
	int hcuv_last_cycle;
	int ocd_count;
	int ocd_last_cycle;
	int hocd_count;
	int hocd_last_cycle;
	int hscd_count;
	int hscd_last_cycle;
};

enum manu_macro {
	TERMINATION = 0,
	RECHARGE_VOL,
	FFC_TERMINATION,
	MANU_NAME,
	MANU_DATA_LEN,
};

enum fw_status_elem {
	FG_FW_INIT,
	FG_FW_ERROR,
	FG_FW_SUCCESS,
	FG_FW_INVALID,

};

#define TERMINATION_BYTE	6
#define TERMINATION_BASE	30
#define TERMINATION_STEP	5

#define RECHARGE_VOL_BYTE	7
#define RECHARGE_VOL_BASE	4200
#define RECHARGE_VOL_STEP	5

#define FFC_TERMINATION_BYTE	8
#define FFC_TERMINATION_BASE	400
#define FFC_TERMINATION_STEP	20

#define MANU_NAME_BYTE		3
#define MANU_NAME_BASE		0x0C
#define MANU_NAME_STEP		1

struct manu_data {
	int byte;
	int base;
	int step;
	int data;
};

static struct manu_data manu_info[MANU_DATA_LEN] = {
	{TERMINATION_BYTE, TERMINATION_BASE, TERMINATION_STEP},
	{RECHARGE_VOL_BYTE, RECHARGE_VOL_BASE, RECHARGE_VOL_STEP},
	{FFC_TERMINATION_BYTE, FFC_TERMINATION_BASE, FFC_TERMINATION_STEP},
	{MANU_NAME, MANU_NAME_BASE, MANU_NAME_STEP},
};

#define SHUTDOWN_DELAY_VOL	3300
#define BQ_RESUME_UPDATE_TIME	600

static const u8 fg_dump_regs[] = {
	0x00, 0x02, 0x04, 0x06,
	0x08, 0x0A, 0x0C, 0x0E,
	0x10, 0x16, 0x18, 0x1A,
	0x1C, 0x1E, 0x20, 0x28,
	0x2A, 0x2C, 0x2E, 0x30,
	0x66, 0x68, 0x6C, 0x6E,
};

struct fg_batt_info {
	int curr;
	int volt;
	int vcell_max;
	int temp;
	int rsoc;
	int rm;
	int fcc;
	int cycle_count;
	int soh;
};

#endif /* __BQ27Z561_H */
