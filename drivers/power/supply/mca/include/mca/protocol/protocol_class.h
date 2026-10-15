#ifndef _MCA_PROTOCOL_PROTOCOL_CLASS_H_
#define _MCA_PROTOCOL_PROTOCOL_CLASS_H_

#include <linux/types.h>

enum adapter_protocol_type {
	ADAPTER_PROTOCOL_PD = 0,
	ADAPTER_PROTOCOL_PPS,
	ADAPTER_PROTOCOL_RESERVED_2,
	ADAPTER_PROTOCOL_QC,
	ADAPTER_PROTOCOL_RESERVED_4,
	ADAPTER_PROTOCOL_BC12,
	ADAPTER_PROTOCOL_MAX,
};

enum mca_adapter_type {
	ADAPTER_NONE = 0,
	ADAPTER_SDP,
	ADAPTER_DCP,
	ADAPTER_CDP,
	ADAPTER_QC2,
	ADAPTER_QC3,
	ADAPTER_PD,
	ADAPTER_XIAOMI_QC3,
	ADAPTER_XIAOMI_PD,
	ADAPTER_ZIMI_CAR_POWER,
	ADAPTER_XIAOMI_PD_40W,
	ADAPTER_VOICE_BOX,
	ADAPTER_XIAOMI_PD_50W,
	ADAPTER_XIAOMI_PD_60W,
	ADAPTER_XIAOMI_PD_100W,
	ADAPTER_AUTH_FAILED,
};

enum xm_charger_type {
	XM_CHARGER_TYPE_UNKNOW = 0,
	XM_CHARGER_TYPE_SDP,
	XM_CHARGER_TYPE_CDP,
	XM_CHARGER_TYPE_DCP,
	XM_CHARGER_TYPE_FLOAT,
	XM_CHARGER_TYPE_HVDCP2,
	XM_CHARGER_TYPE_HVDCP3,
	XM_CHARGER_TYPE_HVDCP3_B,
	XM_CHARGER_TYPE_HVDCP3P5,
	XM_CHARGER_TYPE_TYPEC,
	XM_CHARGER_TYPE_PD,
	XM_CHARGER_TYPE_PD_VERIFY,
	XM_CHARGER_TYPE_PPS,
	XM_CHARGER_TYPE_RESERVED_13,
	XM_CHARGER_TYPE_ACA,
	XM_CHARGER_TYPE_OCP,
};

enum typec_port_index {
	TYPEC_PORT_0 = 0,
	TYPEC_PORT_1,
	TYPEC_PORT_MAX,
};

enum typec_attach_mode {
	TYPEC_UNATTACH = 0,
	TYPEC_SNK_MODE,
	TYPEC_SRC_MODE,
	TYPEC_AUDIO_ACCESS_MODE,
	TYPEC_DEBUG_ACCESS_MODE,
};

#define ADAPTER_CAP_MAX_NR 7

struct adapter_power_cap {
	int unknown_0;
	int max_current;
	int max_voltage;
	int min_voltage;
	int max_power;
};

struct adapter_power_cap_info {
	int nums;
	struct adapter_power_cap cap[ADAPTER_CAP_MAX_NR];
};

struct adapter_vendor_info {
	unsigned int vid;
	unsigned int pid;
	unsigned char hw_ver;
	unsigned char fw_ver;
};

struct adapter_power_curve {
	int nums;
	int volt[ADAPTER_CAP_MAX_NR];
	int curr[ADAPTER_CAP_MAX_NR];
};

struct adapter_protocol_class_ops {
	int (*set_adapter_verified)(void *data, int verified);
	int (*get_adapter_verified)(void *data, int *verified);
	int (*get_adapter_max_power)(void *data, unsigned int *max_power);
	int (*get_adapter_pwr_max_power)(void *data, unsigned int *max_power);
	int (*adapter_det_en)(void *data, int en);
	int (*get_adapter_type)(void *data, int *type);
	int (*get_adapter_pwr_cap)(void *data, struct adapter_power_cap_info *cap);
	int (*set_adapter_volt_and_curr)(void *data, int volt, int curr);
	int (*get_adapter_volt_and_curr)(void *data, int *volt, int *curr);
	int (*get_adapter_pps_ptf)(void *data, int *pps_ptf);
	int (*get_adapter_info)(void *data, struct adapter_vendor_info *info);
	int (*get_adapter_power_curve)(void *data,
				       struct adapter_power_curve *pwr_curve);
};

int protocol_class_register_ops(unsigned int protocol,
				struct adapter_protocol_class_ops *ops, void *data);
int protocol_class_set_adapter_verified(unsigned int protocol, int verified);
int protocol_class_get_adapter_verified(unsigned int protocol, int *verified);
int protocol_class_get_adapter_max_power(unsigned int protocol,
					 unsigned int *max_power);
int protocol_class_get_adapter_pwr_max_power(unsigned int protocol,
					     unsigned int *max_power);
int protocol_class_det_adapter_type(unsigned int protocol, int en);
int protocol_class_get_adapter_type(unsigned int protocol, unsigned int *value);
int protocol_class_get_adapter_power_cap(unsigned int protocol,
					 struct adapter_power_cap_info *cap);
int protocol_class_set_adapter_volt_and_curr(unsigned int protocol,
					     int volt, int curr);
int protocol_class_get_adapter_volt_and_curr(unsigned int protocol,
					     int *volt, int *curr);
int protocol_class_get_adapter_pps_ptf(unsigned int protocol, int *pps_ptf);
int protocol_class_get_adapter_info(unsigned int protocol,
				    struct adapter_vendor_info *info);
int protocol_class_get_adapter_power_curve(unsigned int protocol,
					   struct adapter_power_curve *pwr_curve);

void protocol_class_unregister_ops(unsigned int role, void *data);

#endif /* _MCA_PROTOCOL_PROTOCOL_CLASS_H_ */
