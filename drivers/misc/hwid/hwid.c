// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include "hwid.h"

#define HW_MAJOR_VERSION_SHIFT 16
#define HW_MINOR_VERSION_SHIFT 0
#define HW_COUNTRY_VERSION_SHIFT 20
#define HW_BUILD_VERSION_SHIFT 16
#define HW_MAJOR_VERSION_MASK 0xffff0000
#define HW_MINOR_VERSION_MASK 0x0000ffff
#define HW_COUNTRY_VERSION_MASK 0xfff00000
#define HW_BUILD_VERSION_MASK 0x000f0000

static uint project;
module_param(project, uint, 0444);

static char project_name[16];
module_param_string(project_name, project_name, sizeof(project_name), 0444);

static uint hwid_value;
module_param(hwid_value, uint, 0444);

static uint build_adc;
module_param(build_adc, uint, 0444);
MODULE_PARM_DESC(build_adc, "Xiaomi ADC value of build resistance");

static uint project_adc;
module_param(project_adc, uint, 0444);
MODULE_PARM_DESC(project_adc, "Xiaomi ADC value of project resistance");

const char *product_name_get(void)
{
	return project_name;
}
EXPORT_SYMBOL(product_name_get);

uint32_t get_hw_version_platform(void)
{
	return project;
}
EXPORT_SYMBOL(get_hw_version_platform);

uint32_t get_hw_project_adc(void)
{
	return project_adc;
}
EXPORT_SYMBOL(get_hw_project_adc);

uint32_t get_hw_build_adc(void)
{
	return build_adc;
}
EXPORT_SYMBOL(get_hw_build_adc);

uint32_t get_hw_id_value(void)
{
	return hwid_value;
}
EXPORT_SYMBOL(get_hw_id_value);

uint32_t get_hw_country_version(void)
{
	return (hwid_value & HW_COUNTRY_VERSION_MASK) >> HW_COUNTRY_VERSION_SHIFT;
}
EXPORT_SYMBOL(get_hw_country_version);

uint32_t get_hw_version_major(void)
{
	return (hwid_value & HW_MAJOR_VERSION_MASK) >> HW_MAJOR_VERSION_SHIFT;
}
EXPORT_SYMBOL(get_hw_version_major);

uint32_t get_hw_version_minor(void)
{
	return (hwid_value & HW_MINOR_VERSION_MASK) >> HW_MINOR_VERSION_SHIFT;
}
EXPORT_SYMBOL(get_hw_version_minor);

uint32_t get_hw_version_build(void)
{
	return (hwid_value & HW_BUILD_VERSION_MASK) >> HW_BUILD_VERSION_SHIFT;
}
EXPORT_SYMBOL(get_hw_version_build);

MODULE_DESCRIPTION("Xiaomi hardware identification module");
MODULE_LICENSE("GPL v2");
