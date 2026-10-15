/* SPDX-License-Identifier: GPL-2.0 */
/* Single-pack policy adapted from Xiaomi/Onyx mca_buckchg_jeita. */
#ifndef _MCA_JEITA_POLICY_H_
#define _MCA_JEITA_POLICY_H_

#define MCA_JEITA_MAX_BANDS 15
#define MCA_JEITA_MAX_VOLTAGES 4

struct mca_jeita_voltage {
	int voltage;
	int charge_current;
};

struct mca_jeita_band {
	int temp_low;
	int temp_high;
	int low_hyst;
	int high_hyst;
	int charge_current;
	int vterm;
	int iterm;
	int voltage_count;
	struct mca_jeita_voltage voltages[MCA_JEITA_MAX_VOLTAGES];
};

struct mca_jeita_table {
	int count;
	struct mca_jeita_band bands[MCA_JEITA_MAX_BANDS];
};

/* BAA supplies termination overrides for an existing DT row. */
static inline int mca_jeita_update_term(struct mca_jeita_table *table,
				      int index, int vterm, int iterm)
{
	struct mca_jeita_band *band;

	if (index < 0 || index >= table->count || vterm <= 0 || iterm < 0)
		return 0;
	band = &table->bands[index];
	if (band->voltage_count > 1 &&
	    band->voltages[band->voltage_count - 2].voltage >= vterm)
		return 0;
	band->vterm = vterm;
	band->iterm = iterm;
	if (band->voltage_count)
		band->voltages[band->voltage_count - 1].voltage = vterm;
	return 1;
}

static inline int mca_jeita_table_valid(const struct mca_jeita_table *table)
{
	int i, j;

	if (table->count < 0 || table->count > MCA_JEITA_MAX_BANDS)
		return 0;
	for (i = 0; i < table->count; i++) {
		const struct mca_jeita_band *band = &table->bands[i];

		/* Bounds protect subsequent Celsius -> deci-Celsius arithmetic. */
		if (band->temp_low < -65535 || band->temp_high > 65535 ||
		    band->temp_low >= band->temp_high ||
		    band->low_hyst < 0 || band->low_hyst > 1000 ||
		    band->high_hyst < 0 || band->high_hyst > 1000 ||
		    band->charge_current < 0 || band->vterm <= 0 || band->iterm < 0 ||
		    band->voltage_count < 0 ||
		    band->voltage_count > MCA_JEITA_MAX_VOLTAGES)
			return 0;
		if (i && table->bands[i - 1].temp_high > band->temp_low)
			return 0;
		for (j = 0; j < band->voltage_count; j++) {
			if (band->voltages[j].voltage <= 0 ||
			    band->voltages[j].charge_current < 0 ||
			    (j && band->voltages[j - 1].voltage >=
				  band->voltages[j].voltage))
				return 0;
		}
	}
	return 1;
}

/* Gauge temperature is in deci-Celsius; DT ranges/hysteresis are Celsius. */
static inline int mca_jeita_find_band(const struct mca_jeita_table *table,
				    int temperature)
{
	int i;

	for (i = 0; i < table->count; i++)
		if (temperature >= table->bands[i].temp_low * 10 &&
		    temperature < table->bands[i].temp_high * 10)
			return i;
	return -1;
}

static inline int mca_jeita_select_band(const struct mca_jeita_table *table,
				      int temperature, int previous,
				      int extreme_cold)
{
	int next = mca_jeita_find_band(table, temperature);
	const struct mca_jeita_band *old;

	if (next < 0 || previous < 0 || previous >= table->count ||
	    next == previous || !table->bands[next].charge_current)
		return next;
	old = &table->bands[previous];
	/* Never hold a stale band across a multi-zone temperature jump. */
	if (next == previous + 1 && !(previous == 0 && extreme_cold) &&
	    temperature < (old->temp_high + old->high_hyst) * 10)
		return previous;
	if (next == previous - 1 &&
	    temperature > (old->temp_low - old->low_hyst) * 10)
		return previous;
	return next;
}

static inline int mca_jeita_select_current(const struct mca_jeita_band *band,
					 int voltage, int previous,
					 int low_hyst)
{
	int i, charge_current = band->charge_current;

	for (i = 0; i < band->voltage_count; i++) {
		if (voltage <= band->voltages[i].voltage) {
			charge_current = band->voltages[i].charge_current;
			if (charge_current > band->charge_current)
				charge_current = band->charge_current;
			if (previous >= 0 && charge_current > previous &&
			    (long long)voltage + low_hyst >=
				band->voltages[i].voltage)
				return previous;
			return charge_current;
		}
		if (band->voltages[i].charge_current < charge_current)
			charge_current = band->voltages[i].charge_current;
	}
	return charge_current;
}

#endif /* _MCA_JEITA_POLICY_H_ */
