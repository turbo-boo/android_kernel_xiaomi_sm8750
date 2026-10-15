/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_smart_charge.h
 *
 * smart charge driver
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

#ifndef __SMART_CHARGE_H__
#define __SMART_CHARGE_H__

enum SMART_CHG_MODE {
	SMART_CHG_NIGHT, // Warning: smart night not a smart charge feature
	SMART_CHG_NAVIGATION = 1,
	SMART_CHG_OUTDOOR = 2,
	SMART_CHG_LOWFAST = 3,
	SMART_CHG_ENDURANCE_PRO = 4,
	SMART_CHG_WLS_SUPER = 5,
	SMART_CHG_SENSE_CHG = 6,
	SMART_CHG_WLS_QUIET = 7,
	SMART_CHG_EXTREME_COLD = 8,
	SMART_CHG_TRAVELWAIT = 9,
	SMART_CHG_MAX = 15,
};

union SMART_CHG_HEADER {
	unsigned long AsUINT32;
	struct {
		unsigned long enable : 1; /* BIT0: en_ret*/
		unsigned long func_type : 15; /* BIT1 - BIT15: func_typ */
		unsigned long func_value : 16; /* BIT16 - BIT31: func_val */
	};
	struct {
		/* BIT0: en_ret*/
		unsigned long : 1; /*bit [0:0]*/
		/* BIT1 - BIT15: func_typ */
		unsigned long navigation : 1; /*bit [1:1]*/
		unsigned long outdoor : 1; /*bit [2:2]*/
		unsigned long lowfast : 1; /*bit [3:3]*/
		unsigned long endurance_pro : 1; /*bit [4:4]*/
		unsigned long wls_super_chg : 1; /*bit [5:5]*/
		unsigned long sense_chg : 1; /*bit [6:6]*/
		unsigned long wls_quiet : 1; /*bit [7:7]*/
		unsigned long extreme_cold : 1; /*bit [8:8]*/
		unsigned long travel_wait : 1; /*bit [9:9]*/
		unsigned long smart_bypass : 1; /*bit [10:10]*/
		unsigned long : 5; /*bit [15:11]*/
		/* BIT16 - BIT31: func_val */
		unsigned long soc_limit : 8; /*bit [23:16]*/
		unsigned long : 8; /*bit [31:24]*/
	};
};

union SMART_CHG_MIEVENT {
	unsigned long AsUINT32;
	struct {
		unsigned long : 1; /*bit [0:0]*/
		/* BIT1 - BIT15: func_typ */
		unsigned long navigation : 1; /*bit [1:1]*/
		unsigned long outdoor : 1; /*bit [2:2]*/
		unsigned long lowfast : 1; /*bit [3:3]*/
		unsigned long endurance_pro : 1; /*bit [4:4]*/
		unsigned long wls_super_chg : 1; /*bit [5:5]*/
		unsigned long sense_chg : 1; /*bit [6:6]*/
		unsigned long wls_quiet : 1; /*bit [7:7]*/
		unsigned long extreme_cold : 1; /*bit [8:8]*/
		unsigned long travel_wait : 1; /*bit [9:9]*/
	};
};

enum smart_chg_posture_stat {
	SMART_CHG_POSTURE_UNKNOW,
	SMART_CHG_POSTURE_DESKTOP = 1,
	SMART_CHG_POSTURE_HOLDER = 2,
	SMART_CHG_POSTURE_ONEHAND = 3,
	SMART_CHG_POSTURE_TWOHAND_H = 4,
	SMART_CHG_POSTURE_TWOHAND_V = 5,
	SMART_CHG_POSTURE_ANS_CALL = 6,
	SMART_CHG_POSTURE_INDEX_MAX,
	SMART_CHG_POSTURE_FAKE_CLEAR = 100,
	SMART_CHG_POSTURE_FAKE_DESKTOP = 101,
	SMART_CHG_POSTURE_FAKE_HOLDER = 102,
	SMART_CHG_POSTURE_FAKE_ONEHAND = 103,
	SMART_CHG_POSTURE_FAKE_TWOHAND_H = 104,
	SMART_CHG_POSTURE_FAKE_TWOHAND_V = 105,
	SMART_CHG_POSTURE_FAKE_ANS_CALL = 106,
	SMART_CHG_POSTURE_FAKE_INDEX_MAX,
};

enum smart_chg_scene_type {
	SMART_CHG_SCENE_NORMAL = 0,
	SMART_CHG_SCENE_HUANJI = 1,
	SMART_CHG_SCENE_PHONE = 5,
	SMART_CHG_SCENE_NOLIMIT = 6,
	SMART_CHG_SCENE_CLASS0 = 7,
	SMART_CHG_SCENE_YOUTUBE = 8,
	SMART_CHG_SCENE_NAVIGATION = 10,
	SMART_CHG_SCENE_VIDEO = 11,
	SMART_CHG_SCENE_VIDEOCHAT = 14,
	SMART_CHG_SCENE_CAMERA = 15,
	SMART_CHG_SCENE_TGAME = 18,
	SMART_CHG_SCENE_MGAME = 19,
	SMART_CHG_SCENE_YUANSHEN = 20,
	SMART_CHG_SCENE_XINGTIE = 25,
	SMART_CHG_SCENE_PER_NORMAL = 50,
	SMART_CHG_SCENE_PER_CLASS0 = 57,
	SMART_CHG_SCENE_PER_YOUTUBE = 58,
	SMART_CHG_SCENE_PER_VIDEO = 61,
	SMART_CHG_SCENE_INDEX_MAX,
	SMART_CHG_SCENE_REDIR_START = 1000,
	SMART_CHG_SCENE_REDIR_INDEX_MAX =
		SMART_CHG_SCENE_REDIR_START + SMART_CHG_SCENE_INDEX_MAX,
};

enum smart_chg_sic_mode {
	SMART_CHG_SIC_MODE_BALANCED = 0,
	SMART_CHG_SIC_MODE_SLIGHTCHG = 2,
	SMART_CHG_SIC_MODE_MIDDLE = 4,
	SMART_CHG_SIC_MODE_SUPERCHG = 8,
	SMART_CHG_SIC_MODE_MODE_MAX_INDEX,
};

enum smart_chg_pwr_boost_type {
	SMART_CHG_OUTDOOR_PWR_BOOST = 0,
	SMART_CHG_TRAVELWAIT_PWR_BOOST = 1,
	SMART_CHG_PWR_BOOST_MAX_INDEX,
};

struct SMART_CHG_DATA {
	uint16_t enable;
	uint16_t func_value;
	bool use_fake_value;
	int fake_value;
};

struct SMART_CHG_INFO {
	uint8_t ret_code;
	struct SMART_CHG_DATA smart_chgcfg[SMART_CHG_MAX];
};

struct ICHG_CC_CFG {
	int cc_max;
	int delta_ichg;
};

#define CC_ICHG_MAX_GROUP 2

#define SMART_BYPASS_TEMP_SECTION_MAX 10
#define SMART_BYPASS_DEBOUNCE_NS (10 * NSEC_PER_SEC)
#define SMART_CHG_BOARD_TEMP_SCALE 100

struct smart_bypass_temp_section {
	int temp_low;
	int temp_high;
	int hyst_low;
	int hyst_high;
	int fcc;
};

struct smart_charge_info {
	bool stopping;
	struct class *smart_charge_class;
	struct cdev *pri_dev;
	struct device *sys_dev;
	struct device *dev;
	dev_t dev_num;
	struct delayed_work smart_charge_work;
	struct delayed_work smart_sense_chg_work;
	struct delayed_work smart_soc_limit_work;
	struct mca_votable *smartchg_delta_fv_voter;
	struct mca_votable *smartchg_delta_ichg_voter;
	struct notifier_block panel_nb;

	int delta_fv;
	int delta_ichg;
	int soc_limit;
	int online;
	int cycle_count;
	int cell_type;
	int smart_sic_mode;
	int screen_state;
	int scene;
	int fake_scene;
	int board_temp;
	int enable_fv_dec_by_cc;

	int smart_night_val;
	int smart_batt_val;
	int smart_delta_ichg;

	bool support_cc_ichg;
	bool support_sensechg_v2;
	bool soc_limit_enable;
	char reserve[2];
	struct ICHG_CC_CFG ichg_cc_table[CC_ICHG_MAX_GROUP];
	/* Smart Charging Engine */
	union SMART_CHG_HEADER smart_chg_control;
	struct SMART_CHG_INFO smart_chg_data;
	char *mmap_addr;
	int map_flag;
	int pulse_mode;
	int support_csd;
	int plugin_rsoc;
	int night_enable_rsoc;
	union SMART_CHG_MIEVENT ignore_upload;
	size_t mmap_size;
	struct mca_votable *smartchg_set_fcc_voter;
	struct notifier_block thermal_nb;
	int wls_online;
	int support_bypass;
	int bypass_entry_soc;
	int bypass_exit_soc;
	uint16_t bypass_enable;
	uint16_t bypass_reserved;
	uint16_t last_bypass_state;
	int bypass_active;
	int bypass_temp_index;
	int last_bypass_fcc;
	ktime_t bypass_start_time;
	bool bypass_exit_flag;
	int bypass_high_num;
	int bypass_med_num;
	int bypass_low_num;
	struct smart_bypass_temp_section
		bypass_high_lmt[SMART_BYPASS_TEMP_SECTION_MAX];
	struct smart_bypass_temp_section
		bypass_med_lmt[SMART_BYPASS_TEMP_SECTION_MAX];
	struct smart_bypass_temp_section
		bypass_low_lmt[SMART_BYPASS_TEMP_SECTION_MAX];
	int mishow_config;
};

enum smartchg_attr_list {
	MCA_PROP_SMARTCHG,
	MCA_PROP_SMARTCHG_FV,
	MCA_PROP_SMARTCHG_ICHG,
	MCA_PROP_SMARTBATT,
	MCA_PROP_SMARTNIGHT,
	MCA_PROP_POSTURE,
	MCA_PROP_SCENE,
	MCA_PROP_BOARD_TEMP,
	MCA_PROP_SMART_SIC_MODE,
};

enum smartchg_cell_type {
	MCA_CELL_TYPE_1S = 1,
	MCA_CELL_TYPE_2S,
	MCA_CELL_TYPE_MAX,
};

enum smartchg_cc_ichg_cfg_mode_ele {
	SMARTCHG_MODE_CYCLECOUNT,
	SMARTCHG_MODE_DELTA_ICHG,
	SMARTCHG_MODE_PARA_MAX,
};

#endif /* __SMART_CHARGE_H__ */
