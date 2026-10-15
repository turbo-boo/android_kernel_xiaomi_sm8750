// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../../../../drivers/power/supply/mca/mca_strategy/strategy_jeita/mca_jeita_policy.h"
#include "dada_jeita_tables.h"

static unsigned int checks;
#define CHECK(condition) do { assert(condition); checks++; } while (0)

int main(void)
{
	struct mca_jeita_table table = {
		.count = 5,
		.bands = {
			{ .temp_low = -20, .temp_high = 0, .high_hyst = 2,
			  .charge_current = 0, .vterm = 4400, .iterm = 200 },
			{ .temp_low = 0, .temp_high = 10, .low_hyst = 2, .high_hyst = 2,
			  .charge_current = 700, .vterm = 4400, .iterm = 200 },
			{ .temp_low = 10, .temp_high = 45, .low_hyst = 2, .high_hyst = 2,
			  .charge_current = 2000, .vterm = 4500, .iterm = 200,
			  .voltage_count = 3,
			  .voltages = { {4000, 2000}, {4200, 1000}, {4500, 500} } },
			{ .temp_low = 45, .temp_high = 60, .low_hyst = 2, .high_hyst = 2,
			  .charge_current = 1000, .vterm = 4200, .iterm = 200 },
			{ .temp_low = 60, .temp_high = 80,
			  .charge_current = 0, .vterm = 4200, .iterm = 200 },
		},
	};
	struct mca_jeita_table copy;
	struct mca_jeita_band band = table.bands[2];

	CHECK(mca_jeita_table_valid(&table));
	CHECK(mca_jeita_find_band(&table, -201) == -1);
	CHECK(mca_jeita_find_band(&table, -200) == 0);
	CHECK(mca_jeita_find_band(&table, -1) == 0);
	CHECK(mca_jeita_find_band(&table, 0) == 1);
	CHECK(mca_jeita_find_band(&table, 99) == 1);
	CHECK(mca_jeita_find_band(&table, 100) == 2);
	CHECK(mca_jeita_find_band(&table, 449) == 2);
	CHECK(mca_jeita_find_band(&table, 450) == 3);
	CHECK(mca_jeita_find_band(&table, 600) == 4);
	CHECK(mca_jeita_find_band(&table, 800) == -1);
	CHECK(mca_jeita_select_band(&table, 10, 0, 0) == 0);
	CHECK(mca_jeita_select_band(&table, 20, 0, 0) == 1);
	CHECK(mca_jeita_select_band(&table, 10, 0, 1) == 1);
	CHECK(mca_jeita_select_band(&table, 460, 2, 0) == 2);
	CHECK(mca_jeita_select_band(&table, 470, 2, 0) == 3);
	CHECK(mca_jeita_select_band(&table, 440, 3, 0) == 3);
	CHECK(mca_jeita_select_band(&table, 430, 3, 0) == 2);
	/* Hard stop and large temperature jumps must bypass hysteresis. */
	CHECK(mca_jeita_select_band(&table, -1, 1, 0) == 0);
	CHECK(mca_jeita_select_band(&table, 600, 3, 0) == 4);
	CHECK(mca_jeita_select_band(&table, 600, 1, 0) == 4);
	CHECK(mca_jeita_select_band(&table, 900, 2, 0) == -1);
	CHECK(mca_jeita_select_band(&table, 200, 99, 0) == 2);

	CHECK(mca_jeita_select_current(&band, 3950, -1, 50) == 2000);
	CHECK(mca_jeita_select_current(&band, 4000, -1, 50) == 2000);
	CHECK(mca_jeita_select_current(&band, 4001, 2000, 50) == 1000);
	CHECK(mca_jeita_select_current(&band, 3950, 1000, 50) == 1000);
	CHECK(mca_jeita_select_current(&band, 3949, 1000, 50) == 2000);
	CHECK(mca_jeita_select_current(&band, 4201, 1000, 50) == 500);
	CHECK(mca_jeita_select_current(&band, 4501, -1, 50) == 500);
	/* Row zero and above-last-threshold must never access index -1. */
	CHECK(mca_jeita_select_current(&band, 3900, 3000, 50) == 2000);
	CHECK(mca_jeita_select_current(&band, INT_MAX, -1, 50) == 500);
	band.voltage_count = 0;
	CHECK(mca_jeita_select_current(&band, 3900, -1, 50) == 2000);
	band.charge_current = 0;
	CHECK(mca_jeita_select_current(&band, 3900, -1, 50) == 0);

	copy = table;
	copy.count = MCA_JEITA_MAX_BANDS + 1;
	CHECK(!mca_jeita_table_valid(&copy));
	copy = table;
	copy.bands[1].temp_low = -1;
	CHECK(!mca_jeita_table_valid(&copy));
	copy = table;
	copy.bands[2].voltages[1].voltage = 4000;
	CHECK(!mca_jeita_table_valid(&copy));
	copy = table;
	copy.bands[2].voltage_count = MCA_JEITA_MAX_VOLTAGES + 1;
	CHECK(!mca_jeita_table_valid(&copy));
	copy = table;
	copy.bands[1].charge_current = -1;
	CHECK(!mca_jeita_table_valid(&copy));
	copy = table;
	CHECK(!mca_jeita_update_term(&copy, -1, 4400, 100));
	CHECK(!mca_jeita_update_term(&copy, copy.count, 4400, 100));
	CHECK(!mca_jeita_update_term(&copy, 2, 4200, 100));
	CHECK(!mca_jeita_update_term(&copy, 2, 4400, -1));
	CHECK(!memcmp(&copy, &table, sizeof(table)));
	CHECK(mca_jeita_update_term(&copy, 2, 4400, 100));
	CHECK(copy.bands[2].vterm == 4400 && copy.bands[2].iterm == 100);
	CHECK(copy.bands[2].voltages[2].voltage == 4400);
	CHECK(copy.bands[2].temp_low == table.bands[2].temp_low);
	CHECK(copy.bands[2].charge_current == table.bands[2].charge_current);
	CHECK(mca_jeita_table_valid(&copy));

	/* Actual Dada CN/Global DT tables, including sentinel bounds and FFC. */
	CHECK(mca_jeita_table_valid(&dada_cn_normal));
	CHECK(mca_jeita_table_valid(&dada_cn_ffc));
	CHECK(mca_jeita_table_valid(&dada_global_normal));
	CHECK(mca_jeita_table_valid(&dada_global_ffc));
	CHECK(mca_jeita_find_band(&dada_cn_normal, -655350) == 0);
	CHECK(mca_jeita_find_band(&dada_cn_normal, -655351) == -1);
	CHECK(mca_jeita_find_band(&dada_cn_normal, 655349) == 7);
	CHECK(mca_jeita_find_band(&dada_cn_normal, 655350) == -1);
	CHECK(mca_jeita_find_band(&dada_cn_normal, -91) == 0);
	CHECK(mca_jeita_find_band(&dada_cn_normal, -90) == 1);
	CHECK(mca_jeita_select_band(&dada_cn_normal, 550, 6, 0) == 7);
	CHECK(mca_jeita_select_current(&dada_cn_normal.bands[1], 4250, -1, 50) == 518);
	CHECK(mca_jeita_find_band(&dada_cn_ffc, 149) == -1);
	CHECK(mca_jeita_find_band(&dada_cn_ffc, 150) == 0);
	CHECK(dada_cn_normal.bands[5].vterm == 4510);
	CHECK(dada_global_normal.bands[5].vterm == 4480);
	CHECK(dada_cn_ffc.bands[0].vterm == 4580);
	CHECK(dada_global_ffc.bands[0].vterm == 4530);

	printf("JEITA policy: %u checks passed\n", checks);
	return 0;
}
