#ifndef _MCA_PROTOCOL_PROTOCOL_PD_CLASS_H_
#define _MCA_PROTOCOL_PROTOCOL_PD_CLASS_H_

#include <linux/types.h>
#include <mca/protocol/protocol_class.h>

#define PROTOCOL_PD_MAX_PDO_NUMS 7
#define PROTOCOL_PD_MAX_STRING_LEN 128
#define USBPD_UVDM_SS_LEN 8
#define USBPD_UVDM_AUTH_FIRST 4
#define USBPD_UVDM_AUTH_WORDS 4

enum mca_pd_dr_request {
	XM_REQUEST_PD_DR_UNKNOWN = 0,
	XM_REQUEST_PD_DR_UFP,
	XM_REQUEST_PD_DR_DFP,
};

enum uvdm_state {
	USBPD_UVDM_DISCONNECT = 0,
	USBPD_UVDM_CHARGER_VERSION,
	USBPD_UVDM_CHARGER_VOLTAGE,
	USBPD_UVDM_CHARGER_TEMP,
	USBPD_UVDM_SESSION_SEED,
	USBPD_UVDM_AUTHENTICATION,
	USBPD_UVDM_VERIFIED,
	USBPD_UVDM_REMOVE_COMPENSATION,
	USBPD_UVDM_REVERSE_AUTHEN,
	USBPD_UVDM_CONNECT,
	USBPD_UVDM_NAN_ACK,
	USBPD_UVDM_CMD_INIT = 50,
	USBPD_UVDM_CMD_NAK = 100,
	USBPD_UVDM_REQUEST_PD_DR = 200,
	USBPD_UVDM_RESET_VSAFE0V = 0xff,
};

struct pd_pdo {
	int min_volt;
	int max_volt;
	int max_current;
};

struct usbpd_vdm_data {
	int ta_version;
	int ta_temp;
	int ta_voltage;
	int reauth;
	unsigned long s_secert[8];
	unsigned long digest[8];
	int svid;
	int ops;
	int cnt;
	u32 vdos[7];
};

struct protocol_class_pd_ops {
	int (*protocol_pd_pps_get_max_power)(unsigned int *max_power, void *data);
	int (*protocol_pd_pps_pdo_select)(int volt, int curr, void *data);
	int (*protocol_pd_get_pps_ptf)(int *pps_ptf, void *data);
	int (*protocol_pd_fixed_pdo_set_vol)(int volt, void *data);
	int (*protocol_pd_set_gear_shift)(int gear_shift, void *data);
	int (*protocol_pd_set_pps_max_cur)(unsigned int curr, void *data);
	int (*protocol_pd_get_pps_max_cur)(unsigned int *curr, void *data);
	int (*protocol_pd_get_pps_status)(int *volt, int *curr, void *data);
	int (*protocol_pd_set_pd_active)(int pd_active, void *data);
	int (*protocol_pd_get_pd_active)(int *pd_active, void *data);
	int (*protocol_pd_set_pps_min_volt)(unsigned int volt, void *data);
	int (*protocol_pd_get_pps_min_volt)(unsigned int *volt, void *data);
	int (*protocol_pd_set_pps_max_volt)(unsigned int volt, void *data);
	int (*protocol_pd_get_pps_max_volt)(unsigned int *volt, void *data);
	int (*protocol_pd_set_pps_apdo_max)(unsigned int apdo_max, void *data);
	int (*protocol_pd_get_pps_apdo_max)(unsigned int *apdo_max, void *data);
	int (*protocol_pd_get_pd_type)(int *type, void *data);
	int (*protocol_pd_set_typec_mode)(int typec_mode, void *data);
	int (*protocol_pd_get_typec_mode)(int *typec_mode, void *data);
	int (*protocol_pd_get_typec_cc_orientation)(int *cc_orientation, void *data);
	int (*protocol_pd_set_typec_cc_orientation)(int cc_orientation, void *data);
	int (*protocol_pd_set_in_hard_reset)(int in_hard_reset, void *data);
	int (*protocol_pd_get_in_hard_reset)(int *in_hard_reset, void *data);
	int (*protocol_pd_set_usb_suspend_supported)(int supported, void *data);
	int (*protocol_pd_get_usb_suspend_supported)(int *supported, void *data);
	int (*protocol_pd_set_pd_typec_accessory_mode)(int typec_accessory_mode,
						     void *data);
	int (*protocol_pd_get_pd_typec_accessory_mode)(int *typec_accessory_mode,
						     void *data);
	int (*protocol_pd_get_cap)(int cap_type, struct adapter_power_cap *tacap,
				   void *data);
	int (*protocol_pd_get_adapter_id)(unsigned int *adapter_id, void *data);
	int (*protocol_pd_get_adapter_svid)(unsigned int *adapter_svid, void *data);
	int (*protocol_pd_request_vdm_cmd)(enum uvdm_state cmd,
					   unsigned int *data,
					   unsigned int data_len, void *priv);
	int (*protocol_pd_get_vdm_cmd)(int *cmd, struct usbpd_vdm_data *vdm_data,
				       void *data);
	int (*protocol_pd_get_power_role)(unsigned char *pr, void *data);
	int (*protocol_pd_get_data_role)(unsigned char *pr, void *data);
	int (*protocol_pd_get_current_state)(char *current_state, int len,
					     void *data);
	int (*protocol_pd_get_pdos)(struct pd_pdo *pdos, int count, void *data);
	int (*protocol_pd_set_verify_process)(int verify_process, void *data);
	int (*protocol_pd_get_verify_process)(int *verify_process, void *data);
	int (*protocol_pd_set_pd_verifed)(int pd_verifed, void *data);
	int (*protocol_pd_get_pd_verifed)(int *pd_verifed, void *data);
	int (*protocol_pd_get_has_dp)(bool *has_dp, void *data);
	int (*protocol_pd_get_cid_status)(bool *status, void *data);
	int (*protocol_pd_get_otg_plugin_status)(bool *status, void *data);
	int (*protocol_pd_set_cc_toggle)(bool en, void *data);
	int (*protocol_pd_get_cc_toggle)(bool *en, void *data);
	int (*protocol_pd_get_snk_src_mode)(int *snk_src_mode, void *data);
	int (*protocol_pd_get_cc_status)(bool *status, void *data);
	int (*protocol_pd_get_cc_short_vbus)(int *cc_short_vbus, void *data);
	int (*protocol_pd_get_suspend_support_status)(bool *pdsuspendsupported,
						      void *data);
	int (*protocol_pd_get_zimi_cypress_flag)(int *zimi_cypress_flag,
						 void *data);
};

int protocol_class_pd_register_ops(unsigned int port_num,
				   struct protocol_class_pd_ops *ops, void *data);
int protocol_class_pd_get_pps_max_power(unsigned int port_num,
					unsigned int *max_power);
int protocol_class_pd_set_pps_pdo_select(unsigned int port_num, int volt,
					 int curr);
int protocol_class_pd_get_pps_ptf(unsigned int port_num, int *pps_ptf);
int protocol_class_pd_set_fixed_volt(unsigned int port_num, int volt);
int protocol_class_pd_set_gear_shift(unsigned int port_num, int gear_shift);
int protocol_class_pd_set_pps_max_cur(unsigned int port_num, unsigned int curr);
int protocol_class_pd_get_pps_max_cur(unsigned int port_num,
					 unsigned int *curr);
int protocol_class_pd_get_pps_status(unsigned int port_num, int *volt,
					int *curr);
int protocol_class_pd_set_pd_active(unsigned int port_num, int pd_active);
int protocol_class_pd_get_pd_active(unsigned int port_num, int *pd_active);
int protocol_class_pd_set_pps_min_volt(unsigned int port_num,
					  unsigned int volt);
int protocol_class_pd_get_pps_min_volt(unsigned int port_num,
					  unsigned int *volt);
int protocol_class_pd_set_pps_max_volt(unsigned int port_num,
					  unsigned int volt);
int protocol_class_pd_get_pps_max_volt(unsigned int port_num,
					  unsigned int *volt);
int protocol_class_pd_set_pps_apdo_max(unsigned int port_num,
					  unsigned int apdo_max);
int protocol_class_pd_get_pps_apdo_max(unsigned int port_num,
					  unsigned int *apdo_max);
int protocol_class_pd_get_pd_type(unsigned int port_num, int *type);
int protocol_class_pd_set_typec_mode(unsigned int port_num, int typec_mode);
int protocol_class_pd_get_typec_mode(unsigned int port_num, int *typec_mode);
int protocol_class_pd_get_typec_cc_orientation(unsigned int port_num,
					       int *cc_orientation);
int protocol_class_pd_set_typec_cc_orientation(unsigned int port_num,
					       int cc_orientation);
int protocol_class_pd_set_pd_in_hard_reset(unsigned int port_num,
					   int in_hard_reset);
int protocol_class_pd_get_pd_in_hard_reset(unsigned int port_num,
					   int *in_hard_reset);
int protocol_class_pd_set_usb_suspend_supported(unsigned int port_num,
						int supported);
int protocol_class_pd_get_usb_suspend_supported(unsigned int port_num,
						int *supported);
int protocol_class_pd_set_pd_typec_accessory_mode(unsigned int port_num,
						  int typec_accessory_mode);
int protocol_class_pd_get_pd_typec_accessory_mode(unsigned int port_num,
						  int *typec_accessory_mode);
int protocol_class_pd_get_cap(unsigned int port_num, int cap_type,
			      struct adapter_power_cap *tacap);
int protocol_class_pd_get_adapter_id(unsigned int port_num,
				     unsigned int *adapter_id);
int protocol_class_pd_get_adapter_svid(unsigned int port_num,
				       unsigned int *adapter_svid);
int protocol_class_pd_request_vdm_cmd(unsigned int port_num,
				      enum uvdm_state cmd, unsigned int *data,
				      unsigned int data_len);
int protocol_class_pd_get_vdm_cmd(unsigned int port_num, int *cmd,
				  struct usbpd_vdm_data *vdm_data);
int protocol_class_pd_get_power_role(unsigned int port_num, unsigned char *pr);
int protocol_class_pd_get_data_role(unsigned int port_num, unsigned char *pr);
int protocol_class_pd_get_current_state(unsigned int port_num,
					char *current_state, int len);
int protocol_class_pd_get_pdos(unsigned int port_num, struct pd_pdo *pdos,
			       int count);
int protocol_class_pd_set_verify_process(unsigned int port_num,
					 int verify_process);
int protocol_class_pd_get_verify_process(unsigned int port_num,
					 int *verify_process);
int protocol_class_pd_set_pd_verifed(unsigned int port_num, int pd_verifed);
int protocol_class_pd_get_pd_verifed(unsigned int port_num, int *pd_verifed);
int protocol_class_pd_get_has_dp(unsigned int port_num, bool *has_dp);
int protocol_class_pd_get_cid_status(unsigned int port_num, bool *status);
int protocol_class_pd_get_otg_plugin_status(unsigned int port_num,
					    bool *status);
int protocol_class_pd_set_cc_toggle(unsigned int port_num, bool en);
int protocol_class_pd_get_cc_toggle(unsigned int port_num, bool *en);
int protocol_class_pd_get_snk_src_mode(unsigned int port_num,
				       int *snk_src_mode);
int protocol_class_pd_get_cc_status(unsigned int port_num, bool *status);
int protocol_class_pd_get_cc_short_vbus(unsigned int port_num,
					int *cc_short_vbus);
int protocol_class_pd_get_suspend_support_status(unsigned int port_num,
						 bool *pdsuspendsupported);
int protocol_class_pd_get_zimi_cypress_flag(unsigned int port_num,
					    int *zimi_cypress_flag);
int protocol_class_pd_get_port_num(void);

void protocol_class_pd_unregister_ops(unsigned int role, void *data);

#endif /* _MCA_PROTOCOL_PROTOCOL_PD_CLASS_H_ */
