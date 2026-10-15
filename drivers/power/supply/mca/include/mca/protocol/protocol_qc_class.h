#ifndef _MCA_PROTOCOL_PROTOCOL_QC_CLASS_H_
#define _MCA_PROTOCOL_PROTOCOL_QC_CLASS_H_

#include <linux/types.h>
#include <mca/protocol/protocol_class.h>

#define QC3_STEP_SIZE 200

enum mca_qc_hvdcp_cmd {
	QC2_FORCE_5V = 0,
	QC3_SINGLE_INCREMENT,
	QC3_SINGLE_DECREMENT,
};

struct protocol_class_qc_ops {
	int (*protocol_qc3_check_class_type)(int *type, void *data);
	int (*protocol_qc_get_qc_type)(int *type, void *data);
	int (*protocol_qc_set_volt)(void *data, int volt);
	int (*protocol_qc_set_volt_cmd)(void *data, int hvdcp_cmd);
};

int protocol_class_qc_register_ops(unsigned int port_num,
				   struct protocol_class_qc_ops *ops, void *data);
int protocol_class_qc3_check_class_type(unsigned int port_num, int *type);
int protocol_class_qc_get_qc_type(unsigned int port_num, int *type);
int protocol_class_qc_set_volt(unsigned int port_num, int volt);
int protocol_class_qc_set_volt_cmd(unsigned int port_num, int hvdcp_cmd);

void protocol_class_qc_unregister_ops(unsigned int role, void *data);

#endif /* _MCA_PROTOCOL_PROTOCOL_QC_CLASS_H_ */
