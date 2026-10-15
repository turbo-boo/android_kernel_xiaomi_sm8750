#ifndef _MCA_COMMON_MCA_SMEM_H_
#define _MCA_COMMON_MCA_SMEM_H_

#include <linux/types.h>

int get_smem_battery_info(int *is_zero_speed);
int get_smem_battery_verify_result(u8 *verified, u8 *chip_ok);
int get_smem_battery_cell_sn(u32 *cell_sn);

#endif
