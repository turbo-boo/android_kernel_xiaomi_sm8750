
#ifndef __XM_SOC_SMOOTH_H
#define __XM_SOC_SMOOTH_H

#define BATT_HIGH_AVG_CURRENT 1000000
#define NORMAL_TEMP_CHARGING_DELTA 10000
#define NORMAL_DISTEMP_CHARGING_DELTA 60000
#define LOW_TEMP_CHARGING_DELTA 20000
#define LOW_TEMP_DISCHARGING_DELTA 40000

#define FG_RAW_SOC_FULL 10000
#define FG_REPORT_FULL_SOC 9400
#define FG_OPTIMIZ_FULL_TIME 64000
#define FG_HOLD_UISOC_100_GAP 120

int fg_ui_soc_smooth(struct strategy_fg *bq, bool prohibit_jump);
int fg_recharge_strategy(struct strategy_fg *bq, int batt_raw_soc);
int strategy_soc_smooth_parse_dt(struct strategy_fg *fg);

#endif /* __XM_SOC_SMOOTH_H */
