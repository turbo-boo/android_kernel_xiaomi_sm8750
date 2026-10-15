#ifndef _MCA_COMMON_MCA_ADSP_GLINK_H_
#define _MCA_COMMON_MCA_ADSP_GLINK_H_

#include <linux/types.h>

struct notifier_block;

enum mca_adsp_prop_id {
	ADSP_PROP_ID_DC_INPUT_CURR_LIMIT = 0x1003,
};

struct mca_adsp_glink_ops {
	void (*glink_state_down)(void *priv);
	void (*glink_state_up)(void *priv);
	void (*notification)(u32 prop_id, void *data, u32 len, void *priv);
};

int mca_adsp_glink_write_prop(int prop_id, void *value, size_t size);
int mca_adsp_glink_read_prop(int prop_id, void *value, size_t size);
int mca_adsp_glink_resister_ops(struct mca_adsp_glink_ops *ops, void *priv);

int mca_adsp_glink_qbg_write_prop(int prop_id, void *value, size_t size);
int mca_adsp_glink_qbg_read_prop(int prop_id, void *value, size_t size);
int mca_adsp_glink_qbg_resister_ops(struct mca_adsp_glink_ops *ops,
				    void *priv);

void mca_adsp_glink_unregister_ops(void *priv);
int pen_charge_state_notifier_register_client(struct notifier_block *nb);
int pen_charge_state_notifier_unregister_client(struct notifier_block *nb);
int mca_pen_charge_state_notify(unsigned long event, void *value);

#endif /* _MCA_COMMON_MCA_ADSP_GLINK_H_ */
