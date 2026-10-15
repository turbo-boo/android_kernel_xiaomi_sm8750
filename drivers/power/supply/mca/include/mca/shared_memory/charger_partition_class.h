#ifndef _MCA_SHARED_MEMORY_CHARGER_PARTITION_CLASS_H_
#define _MCA_SHARED_MEMORY_CHARGER_PARTITION_CLASS_H_

#include <linux/types.h>

#define CHARGER_PARTITION_MAGIC 0x20240725

typedef enum {
	CHARGER_PARTITION_HOST_LK,
	CHARGER_PARTITION_HOST_UEFI,
	CHARGER_PARTITION_HOST_ABL,
	CHARGER_PARTITION_HOST_ADSP,
	CHARGER_PARTITION_HOST_KERNEL,
	CHARGER_PARTITION_HOST_HAL,
	CHARGER_PARTITION_HOST_FRAMEWORK,
	CHARGER_PARTITION_HOST_APPLICATION,
	CHARGER_PARTITION_HOST_LAST,
	CHARGER_PARTITION_HOST_INVALID,
} charger_partition_host_type;

typedef enum {
	CHARGER_PARTITION_HEADER,
	CHARGER_PARTITION_INFO_1,
	CHARGER_PARTITION_INFO_2,
	CHARGER_PARTITION_INFO_3,
	CHARGER_PARTITION_INFO_4,
	CHARGER_PARTITION_INFO_5,
	CHARGER_PARTITION_INFO_6,
	CHARGER_PARTITION_INFO_7,
	CHARGER_PARTITION_INFO_8,
	CHARGER_PARTITION_INFO_9,
	CHARGER_PARTITION_INFO_10,
	CHARGER_PARTITION_INFO_LAST,
	CHARGER_PARTITION_INFO_INVALID,
} charger_partition_info_type;

typedef struct {
	u32 magic;
	u32 version;
	u32 initialized;
	u32 avaliable;
	u32 reserved;
} charger_partition_header;

typedef struct {
	u32 power_off_mode;
	u32 zero_speed_mode;
	u32 mishow;
	u32 double85;
	u32 remove_temp_limit;
	u32 soc_limit;
	u32 memory_test;
	u32 reserved;
} charger_partition_info_1;

typedef struct {
	u32 eu_mode;
	u32 ocd_count[2];
	u32 cuv_count[2];
	u32 hscd_count;
	u32 test;
	u32 reserved;
} charger_partition_info_2;

int charger_partition_alloc(u8 host_type, u8 info_type, uint32_t size);
int charger_partition_dealloc(u8 host_type, u8 info_type, uint32_t size);
void *charger_partition_read(u8 host_type, u8 info_type, uint32_t size);
int charger_partition_write(u8 host_type, u8 info_type, void *buf,
			    uint32_t size);
int charger_partition_get_eu_model(bool *is_eu_model);
int charger_partition_read_double85(int *val);
int charger_partition_write_double85(int val);
int charger_partition_write_remove_temp_limit(int val);
int charger_partition_read_memory_test(int *val);
int charger_partition_write_memory_test(int val);
int charger_partition_read_soc_limit(int *val);
int charger_partition_write_soc_limit(int val);
int charger_partition_get_mishow(bool *mishow);
int charger_partition_read_ocd_count(u32 *ocd0, u32 *ocd1);
int charger_partition_write_ocd_count(u32 ocd0, u32 ocd1);
int charger_partition_read_cuv_count(u32 *cuv0, u32 *cuv1);
int charger_partition_write_cuv_count(u32 cuv0, u32 cuv1);
int charger_partition_read_hscd_count(u32 *hscd);
int charger_partition_write_hcsd_count(u32 hscd);

#endif
