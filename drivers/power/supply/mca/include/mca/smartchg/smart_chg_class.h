#ifndef _MCA_SMARTCHG_SMART_CHG_CLASS_H_
#define _MCA_SMARTCHG_SMART_CHG_CLASS_H_

#include <linux/types.h>

#include <mca/smartchg/basp_wire.h>

enum mca_smartchg_if_chg_type {
	MCA_SMARTCHG_IF_CHG_TYPE_BUCK = 0,
	MCA_SMARTCHG_IF_CHG_TYPE_QC,
	MCA_SMARTCHG_IF_CHG_TYPE_JEITA,
	MCA_SMARTCHG_IF_CHG_TYPE_THERMAL,
	MCA_SMARTCHG_IF_CHG_TYPE_WL_BUCK,
	MCA_SMARTCHG_IF_CHG_TYPE_WL_QC,
	MCA_SMARTCHG_IF_CHG_TYPE_END,
};

struct mca_smartchg_if_ops {
	int type;
	void *data;
	int (*set_delta_fv)(void *data, int val);
	int (*set_delta_ichg)(void *data, int val);
	int (*set_fcc)(void *data, int val);
	int (*set_pwr_boost_sts)(void *data, int en);
	int (*set_soc_limit_sts)(void *data, int en);
	int (*set_wls_quiet_sts)(void *data, int en);
	int (*set_wls_super_sts)(void *data, int en);
	int (*update_baa_para)(void *data, char *baa_para, int ffc_size,
			       int normal_size);
};

int mca_smartchg_if_ops_register(struct mca_smartchg_if_ops *ops);
void mca_smartchg_if_ops_unregister(struct mca_smartchg_if_ops *ops);
void mca_smartchg_set_scene(int scene);
int mca_smartchg_get_scene(void);
void mca_smartchg_set_board_temp(int board_temp);
int mca_smartchg_get_board_temp(void);
int mca_smartchg_is_extreme_cold_enabled(void);
int mca_smartchg_get_limit_soc(void);

#endif
