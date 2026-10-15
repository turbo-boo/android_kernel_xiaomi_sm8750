#ifndef _MCA_PLATFORM_PLATFORM_LOADSW_CLASS_H_
#define _MCA_PLATFORM_PLATFORM_LOADSW_CLASS_H_

#include <linux/types.h>

enum loadsw_role {
	LOADSW_ROLE_MASTER = 0,
	LOADSW_ROLE_SLAVE,
	LOADSW_ROLE_MAX,
};

struct platform_class_loadsw_ops {
	int (*loadsw_get_present)(bool *present, void *data);
	int (*loadsw_get_ibat_limit)(int *ibat_limit, void *data);
	int (*loadsw_set_ibat_limit)(int val, void *data);
	int (*loadsw_set_lowpower_mode)(bool mode, void *data);
	int (*loadsw_get_lowpower_mode)(bool *mode, void *data);
};

int platform_class_loadsw_register_ops(unsigned int role,
				       struct platform_class_loadsw_ops *ops,
				       void *data);
int platform_class_loadsw_get_present(unsigned int role, bool *present);
int platform_class_loadsw_get_ibat_limit(unsigned int role, int *ibat_limit);
int platform_class_loadsw_set_ibat_limit(unsigned int role, int val);
int platform_class_loadsw_set_lowpower_mode(unsigned int role, bool mode);
int platform_class_loadsw_get_lowpower_mode(unsigned int role, bool *mode);

#endif /* _MCA_PLATFORM_PLATFORM_LOADSW_CLASS_H_ */
