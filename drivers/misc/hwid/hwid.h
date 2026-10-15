/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __HWID_H__
#define __HWID_H__

#include <linux/types.h>

#define HARDWARE_PROJECT_UNKNOWN 0
#define HARDWARE_PROJECT_O1 4
#define HARDWARE_PROJECT_O10U 7
#define HARDWARE_PROJECT_O2 1
#define HARDWARE_PROJECT_O3 2 /* dada */
#define HARDWARE_PROJECT_O8 5
#define HARDWARE_PROJECT_O9 8

typedef enum {
	CountryCN = 0x00,
	CountryGlobal = 0x01,
	CountryIndia = 0x02,
	CountryJapan = 0x03,
	INVALID = 0x04,
	CountryIDMax = 0x7fffffff,
} CountryType;

const char *product_name_get(void);
/* Preserve the stock public typedef: modversions hashes typedef names. */
uint32_t get_hw_version_platform(void);
uint32_t get_hw_id_value(void);
uint32_t get_hw_country_version(void);
uint32_t get_hw_version_major(void);
uint32_t get_hw_version_minor(void);
uint32_t get_hw_version_build(void);
uint32_t get_hw_project_adc(void);
uint32_t get_hw_build_adc(void);

#endif /* __HWID_H__ */
