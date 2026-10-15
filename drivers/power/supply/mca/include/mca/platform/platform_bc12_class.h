#ifndef _MCA_PLATFORM_PLATFORM_BC12_CLASS_H_
#define _MCA_PLATFORM_PLATFORM_BC12_CLASS_H_

#include <linux/types.h>

enum bc12_role {
	BC12_MAIN_ROLE = 0,
	BC12_AUX_ROLE,
	BC12_MAX_ROLE,
};

struct platform_bc12_class_ops {
	int (*bc12_det_en)(int en, void *data);
	int (*get_charge_type)(int *value, void *data);
};

int platform_bc12_class_ops_register(unsigned int role,
				     struct platform_bc12_class_ops *ops,
				     void *data);
int platform_bc12_class_det_enable(unsigned int role, int en);
int platform_bc12_class_get_charge_type(unsigned int role, int *type);

void platform_bc12_class_ops_unregister(unsigned int role, void *data);

#endif
