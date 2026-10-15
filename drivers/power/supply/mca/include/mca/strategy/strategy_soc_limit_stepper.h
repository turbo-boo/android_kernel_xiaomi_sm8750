#ifndef _MCA_STRATEGY_STRATEGY_SOC_LIMIT_STEPPER_H_
#define _MCA_STRATEGY_STRATEGY_SOC_LIMIT_STEPPER_H_

static const struct {
	int fcc_value;
	int icl_value;
} soc_limit_stepper_table[] = {
	{ 0, 0 },     { 1990, 850 }, { 1400, 750 }, { 1100, 650 },
	{ 900, 550 }, { 700, 450 },  { 500, 350 },  { 300, 350 },
	{ 200, 350 }, { 100, 250 },  { 0, 250 },
};

#endif /* _MCA_STRATEGY_STRATEGY_SOC_LIMIT_STEPPER_H_ */
