#ifndef _MCA_HARDWARE_HW_PATH_CONTROL_H_
#define _MCA_HARDWARE_HW_PATH_CONTROL_H_

#include <linux/bitops.h>
#include <linux/types.h>

/* Dada DT path_condition encodes these sources as a bitmask. */
typedef enum {
	PATH_CONTROL_USB = BIT(0),
	PATH_CONTROL_WLS = BIT(1),
	PATH_CONTROL_WLS_REV = BIT(2),
	PATH_CONTROL_OTG = BIT(3),
	PATH_CONTROL_VDD = BIT(4),
} CONTROL_SRC;

enum mca_otg_enable_sts {
	OTG_DISABLE = 0,
	OTG_ENABLE,
	OTG_ENABLE_SEQUENCE,
};

int mca_path_control_enable_gate(CONTROL_SRC src, bool enable);

#endif /* _MCA_HARDWARE_HW_PATH_CONTROL_H_ */
