#ifndef _MCA_COMMON_MCA_HWID_H_
#define _MCA_COMMON_MCA_HWID_H_

#include <linux/types.h>

struct mca_hwid {
	uint32_t platform_version;
	uint32_t country_version;
	uint32_t major_version;
	uint32_t minor_version;
	uint32_t build_version;
	uint32_t build_adc;
	uint32_t product_adc;
	uint32_t hwid_value;
	const char *product_name;
};

const struct mca_hwid *mca_get_hwid_info(void);

#endif /* _MCA_COMMON_MCA_HWID_H_ */
