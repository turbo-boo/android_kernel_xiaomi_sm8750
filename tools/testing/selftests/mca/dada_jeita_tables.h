/* SPDX-License-Identifier: GPL-2.0 */
/* JEITA table fixtures from MiCode/kernel_devicetree, dada-v-oss:
 * 233bd52eddce9f081f8e87802796f5994babafa5, qcom/dada-charger-common.dtsi.
 * These are test data only; the driver obtains limits from the device DT.
 */
#ifndef _DADA_JEITA_TEST_TABLES_H_
#define _DADA_JEITA_TEST_TABLES_H_

static const struct mca_jeita_table dada_cn_normal = {
	.count = 8,
	.bands = {
		{ .temp_low = -65535, .temp_high = -9,
		  .low_hyst = 0, .high_hyst = 2, .charge_current = 0,
		  .vterm = 4530, .iterm = 500 },
		{ .temp_low = -9, .temp_high = 0,
		  .low_hyst = 1, .high_hyst = 2, .charge_current = 777,
		  .vterm = 4530, .iterm = 259,
		  .voltage_count = 2,
		  .voltages = { {4200, 777}, {4530, 518} } },
		{ .temp_low = 0, .temp_high = 5,
		  .low_hyst = 1, .high_hyst = 2, .charge_current = 1036,
		  .vterm = 4530, .iterm = 259,
		  .voltage_count = 2,
		  .voltages = { {4200, 1036}, {4530, 777} } },
		{ .temp_low = 5, .temp_high = 10,
		  .low_hyst = 1, .high_hyst = 2, .charge_current = 1554,
		  .vterm = 4530, .iterm = 259,
		  .voltage_count = 2,
		  .voltages = { {4200, 1554}, {4530, 1036} } },
		{ .temp_low = 10, .temp_high = 15,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 4000,
		  .vterm = 4530, .iterm = 259 },
		{ .temp_low = 15, .temp_high = 48,
		  .low_hyst = 2, .high_hyst = 0, .charge_current = 4000,
		  .vterm = 4510, .iterm = 259 },
		{ .temp_low = 48, .temp_high = 55,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 2590,
		  .vterm = 4060, .iterm = 259 },
		{ .temp_low = 55, .temp_high = 65535,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 0,
		  .vterm = 4060, .iterm = 230 },
	},
};

static const struct mca_jeita_table dada_cn_ffc = {
	.count = 6,
	.bands = {
		{ .temp_low = 15, .temp_high = 20,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 4000,
		  .vterm = 4580, .iterm = 1010 },
		{ .temp_low = 20, .temp_high = 35,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 4000,
		  .vterm = 4580, .iterm = 1346 },
		{ .temp_low = 35, .temp_high = 40,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 4000,
		  .vterm = 4580, .iterm = 1554 },
		{ .temp_low = 40, .temp_high = 48,
		  .low_hyst = 2, .high_hyst = 0, .charge_current = 4000,
		  .vterm = 4580, .iterm = 1864 },
		{ .temp_low = 48, .temp_high = 55,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 2590,
		  .vterm = 4060, .iterm = 259 },
		{ .temp_low = 55, .temp_high = 65535,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 0,
		  .vterm = 4060, .iterm = 230 },
	},
};

static const struct mca_jeita_table dada_global_normal = {
	.count = 8,
	.bands = {
		{ .temp_low = -65535, .temp_high = -9,
		  .low_hyst = 0, .high_hyst = 2, .charge_current = 0,
		  .vterm = 4480, .iterm = 500 },
		{ .temp_low = -9, .temp_high = 0,
		  .low_hyst = 1, .high_hyst = 2, .charge_current = 777,
		  .vterm = 4480, .iterm = 259,
		  .voltage_count = 2,
		  .voltages = { {4200, 777}, {4480, 518} } },
		{ .temp_low = 0, .temp_high = 5,
		  .low_hyst = 1, .high_hyst = 2, .charge_current = 1036,
		  .vterm = 4480, .iterm = 259,
		  .voltage_count = 2,
		  .voltages = { {4200, 1036}, {4480, 777} } },
		{ .temp_low = 5, .temp_high = 10,
		  .low_hyst = 1, .high_hyst = 2, .charge_current = 1554,
		  .vterm = 4480, .iterm = 259,
		  .voltage_count = 2,
		  .voltages = { {4200, 1554}, {4480, 1036} } },
		{ .temp_low = 10, .temp_high = 15,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 4000,
		  .vterm = 4480, .iterm = 259 },
		{ .temp_low = 15, .temp_high = 48,
		  .low_hyst = 2, .high_hyst = 0, .charge_current = 4000,
		  .vterm = 4480, .iterm = 259 },
		{ .temp_low = 48, .temp_high = 55,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 2590,
		  .vterm = 4060, .iterm = 259 },
		{ .temp_low = 55, .temp_high = 65535,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 0,
		  .vterm = 4060, .iterm = 230 },
	},
};

static const struct mca_jeita_table dada_global_ffc = {
	.count = 6,
	.bands = {
		{ .temp_low = 15, .temp_high = 20,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 4000,
		  .vterm = 4530, .iterm = 984 },
		{ .temp_low = 20, .temp_high = 35,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 4000,
		  .vterm = 4530, .iterm = 1036 },
		{ .temp_low = 35, .temp_high = 40,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 4000,
		  .vterm = 4530, .iterm = 1295 },
		{ .temp_low = 40, .temp_high = 48,
		  .low_hyst = 2, .high_hyst = 0, .charge_current = 4000,
		  .vterm = 4530, .iterm = 1606 },
		{ .temp_low = 48, .temp_high = 55,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 2590,
		  .vterm = 4060, .iterm = 259 },
		{ .temp_low = 55, .temp_high = 65535,
		  .low_hyst = 2, .high_hyst = 2, .charge_current = 0,
		  .vterm = 4060, .iterm = 230 },
	},
};

#endif /* _DADA_JEITA_TEST_TABLES_H_ */
