// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA ADSP transport for Dada.
 *
 * Stock Dada registers two PMIC-GLINK clients: owner 0x800a for MCA and
 * owner 0x8009 for QBG. Both share the Xiaomi request/response wire ABI,
 * callback lists and link-state handling.
 */
#include <mca/common/mca_workqueue.h>
#include <linux/rculist.h>
#include <mca/common/mca_callback.h>

#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/qcom/qti_pmic_glink.h>
#include <linux/workqueue.h>
#include <mca/common/mca_adsp_glink.h>
#include <mca/common/mca_log.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_adsp_glink"
#endif

#define MCA_ADSP_GLINK_OWNER		0x800a
#define MCA_ADSP_GLINK_QBG_OWNER	0x8009
#define MCA_ADSP_GLINK_MSG_REQ		1
#define MCA_ADSP_GLINK_MSG_NOTIFY	2
#define MCA_ADSP_GLINK_OPCODE_READ	1
#define MCA_ADSP_GLINK_OPCODE_WRITE	2
#define MCA_ADSP_PROP_DATA_LEN		256
#define MCA_ADSP_GLINK_TIMEOUT_MS	250

struct mca_adsp_glink_req_msg {
	struct pmic_glink_hdr hdr;
	u32 property_id;
	u32 seq;
	u8 data[MCA_ADSP_PROP_DATA_LEN];
} __packed;

struct mca_adsp_glink_resp_msg {
	struct pmic_glink_hdr hdr;
	u32 property_id;
	u32 retcode;
	u32 seq;
	u8 data[MCA_ADSP_PROP_DATA_LEN];
} __packed;

struct mca_adsp_glink_notify_msg {
	struct pmic_glink_hdr hdr;
	u32 property_id;
	u8 data[MCA_ADSP_PROP_DATA_LEN];
} __packed;

struct mca_adsp_ops_node {
	struct list_head retired;
	struct list_head node;
	struct mca_adsp_glink_ops *ops;
	void *priv;
};

struct mca_adsp_glink_dev {
	struct device *dev;
	struct pmic_glink_client *client;
	struct pmic_glink_client *qbg_client;
	struct mutex rw_lock;
	struct completion ack;
	struct work_struct sync_work;
	enum pmic_glink_state glink_state;
	u32 seq;
	u32 cur_property_id;
	int retcode;
	u8 read_buf[MCA_ADSP_PROP_DATA_LEN];
};

static struct mca_adsp_glink_dev *g_mca_adsp_glink;
static DEFINE_MUTEX(mca_glink_ops_lock);
DEFINE_STATIC_SRCU(mca_glink_ops_callbacks);
DEFINE_STATIC_SRCU(mca_glink_channel_callbacks);
static LIST_HEAD(mca_ops_list);
static LIST_HEAD(qbg_ops_list);

static struct pmic_glink_client *mca_adsp_client_for_owner(
		struct mca_adsp_glink_dev *mca, u32 owner)
{
	if (owner == MCA_ADSP_GLINK_QBG_OWNER)
		return mca->qbg_client;
	return mca->client;
}

static int mca_adsp_glink_xfer(u32 owner, u32 opcode, int prop_id,
			       void *value, size_t size)
{
	CLASS(mca_callback, callback_scope)(&mca_glink_channel_callbacks);

	struct mca_adsp_glink_dev *mca = READ_ONCE(g_mca_adsp_glink);
	struct mca_adsp_glink_req_msg msg = { 0 };
	struct pmic_glink_client *client;
	unsigned long timeout;
	int ret;

	if (!mca || !value || size >= MCA_ADSP_PROP_DATA_LEN)
		return -EINVAL;

	client = mca_adsp_client_for_owner(mca, owner);
	if (IS_ERR_OR_NULL(client))
		return -ENODEV;

	mutex_lock(&mca->rw_lock);
	if (mca->glink_state != PMIC_GLINK_STATE_UP) {
		ret = -ENOTCONN;
		goto out_unlock;
	}

	mca->seq++;
	msg.hdr.owner = owner;
	msg.hdr.type = MCA_ADSP_GLINK_MSG_REQ;
	msg.hdr.opcode = opcode;
	msg.property_id = prop_id;
	msg.seq = mca->seq;
	if (opcode == MCA_ADSP_GLINK_OPCODE_WRITE)
		memcpy(msg.data, value, size);

	reinit_completion(&mca->ack);
	mca->retcode = 0;
	mca->cur_property_id = prop_id;
	memset(mca->read_buf, 0, sizeof(mca->read_buf));

	ret = pmic_glink_write(client, &msg, sizeof(msg));
	if (ret)
		goto out_clear;

	timeout = wait_for_completion_timeout(
		&mca->ack, msecs_to_jiffies(MCA_ADSP_GLINK_TIMEOUT_MS));
	if (!timeout) {
		mca_log_err("timeout prop=0x%x opcode=%u owner=0x%x\n",
			    prop_id, opcode, owner);
		ret = -ETIMEDOUT;
		goto out_clear;
	}

	ret = mca->retcode;
	if (!ret && opcode == MCA_ADSP_GLINK_OPCODE_READ)
		memcpy(value, mca->read_buf, size);

out_clear:
	mca->cur_property_id = U32_MAX;
out_unlock:
	mutex_unlock(&mca->rw_lock);
	return ret;
}

int mca_adsp_glink_write_prop(int prop_id, void *value, size_t size)
{
	return mca_adsp_glink_xfer(MCA_ADSP_GLINK_OWNER,
				   MCA_ADSP_GLINK_OPCODE_WRITE,
				   prop_id, value, size);
}
EXPORT_SYMBOL(mca_adsp_glink_write_prop);

int mca_adsp_glink_read_prop(int prop_id, void *value, size_t size)
{
	return mca_adsp_glink_xfer(MCA_ADSP_GLINK_OWNER,
				   MCA_ADSP_GLINK_OPCODE_READ,
				   prop_id, value, size);
}
EXPORT_SYMBOL(mca_adsp_glink_read_prop);

int mca_adsp_glink_qbg_write_prop(int prop_id, void *value, size_t size)
{
	return mca_adsp_glink_xfer(MCA_ADSP_GLINK_QBG_OWNER,
				   MCA_ADSP_GLINK_OPCODE_WRITE,
				   prop_id, value, size);
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_write_prop);

int mca_adsp_glink_qbg_read_prop(int prop_id, void *value, size_t size)
{
	return mca_adsp_glink_xfer(MCA_ADSP_GLINK_QBG_OWNER,
				   MCA_ADSP_GLINK_OPCODE_READ,
				   prop_id, value, size);
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_read_prop);

static int mca_adsp_register_ops(struct list_head *head,
				 struct mca_adsp_glink_ops *ops, void *priv)
{
	struct mca_adsp_ops_node *node;

	if (!ops)
		return -EINVAL;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	node->ops = ops;
	node->priv = priv;
	mutex_lock(&mca_glink_ops_lock);
	list_add_tail_rcu(&node->node, head);
	mutex_unlock(&mca_glink_ops_lock);
	return 0;
}

int mca_adsp_glink_resister_ops(struct mca_adsp_glink_ops *ops, void *priv)
{
	return mca_adsp_register_ops(&mca_ops_list, ops, priv);
}
EXPORT_SYMBOL(mca_adsp_glink_resister_ops);

int mca_adsp_glink_qbg_resister_ops(struct mca_adsp_glink_ops *ops, void *priv)
{
	return mca_adsp_register_ops(&qbg_ops_list, ops, priv);
}
EXPORT_SYMBOL(mca_adsp_glink_qbg_resister_ops);

void mca_adsp_glink_unregister_ops(void *priv)
{
	struct mca_adsp_ops_node *node, *next;
	struct list_head *heads[] = { &mca_ops_list, &qbg_ops_list };
	LIST_HEAD(retired);
	int i;

	mutex_lock(&mca_glink_ops_lock);
	for (i = 0; i < ARRAY_SIZE(heads); i++)
		list_for_each_entry_safe(node, next, heads[i], node)
			if (node->priv == priv) {
				list_del_rcu(&node->node);
				list_add_tail(&node->retired, &retired);
			}
	mutex_unlock(&mca_glink_ops_lock);
	synchronize_srcu(&mca_glink_ops_callbacks);
	list_for_each_entry_safe(node, next, &retired, retired) {
		list_del(&node->retired);
		kfree(node);
	}
}
EXPORT_SYMBOL(mca_adsp_glink_unregister_ops);

static void mca_adsp_notify_list(struct list_head *head, u32 prop_id,
				 void *data, u32 len)
{
	CLASS(mca_callback, callback_scope)(&mca_glink_ops_callbacks);

	struct mca_adsp_ops_node *node;

	list_for_each_entry_rcu(node, head, node, srcu_read_lock_held(&mca_glink_ops_callbacks)) {
		if (node->ops && node->ops->notification)
			node->ops->notification(prop_id, data, len, node->priv);
	}
}

static int mca_adsp_handle_notification(struct mca_adsp_glink_notify_msg *msg,
					size_t len)
{
	struct list_head *head;

	if (len < offsetof(struct mca_adsp_glink_notify_msg, data))
		return -EINVAL;

	head = msg->hdr.owner == MCA_ADSP_GLINK_QBG_OWNER ?
		&qbg_ops_list : &mca_ops_list;
	mca_adsp_notify_list(head, msg->property_id, msg->data,
			     min_t(size_t, MCA_ADSP_PROP_DATA_LEN,
				   len - offsetof(struct mca_adsp_glink_notify_msg,
						  data)));
	return 0;
}

static int mca_adsp_handle_response(struct mca_adsp_glink_dev *mca,
				    struct mca_adsp_glink_resp_msg *msg,
				    size_t len)
{
	if (len < offsetof(struct mca_adsp_glink_resp_msg, data))
		return -EINVAL;
	if (msg->seq != mca->seq) {
		mca_log_err("invalid seq_num: %u != %u\n", mca->seq, msg->seq);
		return 0;
	}

	mca->retcode = msg->retcode;
	if (!msg->retcode && msg->hdr.opcode == MCA_ADSP_GLINK_OPCODE_READ)
		memcpy(mca->read_buf, msg->data,
		       min_t(size_t, MCA_ADSP_PROP_DATA_LEN,
			     len - offsetof(struct mca_adsp_glink_resp_msg,
					    data)));
	complete(&mca->ack);
	return 0;
}

static int mca_adsp_glink_callback(void *priv, void *data, size_t len)
{
	struct mca_adsp_glink_dev *mca = priv;
	struct pmic_glink_hdr *hdr = data;

	if (!mca || !data || len < sizeof(*hdr))
		return -EINVAL;
	if (hdr->owner != MCA_ADSP_GLINK_OWNER &&
	    hdr->owner != MCA_ADSP_GLINK_QBG_OWNER)
		return -EINVAL;
	if (hdr->type == MCA_ADSP_GLINK_MSG_NOTIFY)
		return mca_adsp_handle_notification(data, len);
	return mca_adsp_handle_response(mca, data, len);
}

static void mca_adsp_state_notify(struct list_head *head, bool up)
{
	CLASS(mca_callback, callback_scope)(&mca_glink_ops_callbacks);

	struct mca_adsp_ops_node *node;

	list_for_each_entry_rcu(node, head, node, srcu_read_lock_held(&mca_glink_ops_callbacks)) {
		if (!node->ops)
			continue;
		if (up && node->ops->glink_state_up)
			node->ops->glink_state_up(node->priv);
		else if (!up && node->ops->glink_state_down)
			node->ops->glink_state_down(node->priv);
	}
}

static void mca_adsp_sync_work(struct work_struct *work)
{
	struct mca_work_callback mca_work_state;
	CLASS(mca_work_callback, work_scope)(&mca_work_state, work);

	mca_adsp_state_notify(&mca_ops_list, true);
	mca_adsp_state_notify(&qbg_ops_list, true);
}

static void mca_adsp_glink_state_cb(void *priv, enum pmic_glink_state state)
{
	struct mca_adsp_glink_dev *mca = priv;

	mca->glink_state = state;
	if (state == PMIC_GLINK_STATE_UP) {
		queue_work(system_wq, &mca->sync_work);
		return;
	}

	mca_adsp_state_notify(&mca_ops_list, false);
	mca_adsp_state_notify(&qbg_ops_list, false);
}

static int mca_adsp_register_client(struct mca_adsp_glink_dev *mca,
				    struct platform_device *pdev, u32 owner,
				    const char *name,
				    struct pmic_glink_client **client)
{
	struct pmic_glink_client_data data = { 0 };

	data.name = name;
	data.id = owner;
	data.priv = mca;
	data.msg_cb = mca_adsp_glink_callback;
	data.state_cb = mca_adsp_glink_state_cb;

	*client = pmic_glink_register_client(&pdev->dev, &data);
	if (IS_ERR(*client))
		return PTR_ERR(*client);
	return 0;
}

static int mca_adsp_glink_probe(struct platform_device *pdev)
{
	struct mca_adsp_glink_dev *mca;
	int ret;

	mca = devm_kzalloc(&pdev->dev, sizeof(*mca), GFP_KERNEL);
	if (!mca)
		return -ENOMEM;

	mca->dev = &pdev->dev;
	mca->cur_property_id = U32_MAX;
	mca->glink_state = PMIC_GLINK_STATE_UP;
	mutex_init(&mca->rw_lock);
	init_completion(&mca->ack);
	INIT_WORK(&mca->sync_work, mca_adsp_sync_work);

	ret = mca_adsp_register_client(mca, pdev, MCA_ADSP_GLINK_OWNER,
				       "mca_adap_glink", &mca->client);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			mca_log_err("MCA PMIC-GLINK registration failed: %d\n", ret);
		return ret;
	}

	ret = mca_adsp_register_client(mca, pdev, MCA_ADSP_GLINK_QBG_OWNER,
				       "mca_adap_qbg_glink", &mca->qbg_client);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			mca_log_err("QBG PMIC-GLINK registration failed: %d\n", ret);
		pmic_glink_unregister_client(mca->client);
		return ret;
	}

	platform_set_drvdata(pdev, mca);
	g_mca_adsp_glink = mca;
	mca_log_info("registered MCA/QBG owners 0x%x/0x%x\n",
		     MCA_ADSP_GLINK_OWNER, MCA_ADSP_GLINK_QBG_OWNER);
	return 0;
}

static int mca_adsp_glink_remove(struct platform_device *pdev)
{
 struct mca_adsp_glink_dev *mca = platform_get_drvdata(pdev);
 if (!mca)
  return 0;
 WRITE_ONCE(g_mca_adsp_glink, NULL);
 WRITE_ONCE(mca->glink_state, PMIC_GLINK_STATE_DOWN);
 mca->retcode = -ENODEV;
 complete_all(&mca->ack);
 synchronize_srcu(&mca_glink_channel_callbacks);
 if (!IS_ERR_OR_NULL(mca->qbg_client))
  pmic_glink_unregister_client(mca->qbg_client);
 if (!IS_ERR_OR_NULL(mca->client))
  pmic_glink_unregister_client(mca->client);
 cancel_work_sync(&mca->sync_work);
 return 0;
}

static const struct of_device_id mca_adsp_match_table[] = {
	{ .compatible = "mca,adsp_glink" },
	{},
};
MODULE_DEVICE_TABLE(of, mca_adsp_match_table);

static struct platform_driver mca_adsp_driver = {
	.driver = {
		.name = "mca_adsp_glink",
		.of_match_table = mca_adsp_match_table,
	},
	.probe = mca_adsp_glink_probe,
	.remove = mca_adsp_glink_remove,
};
module_platform_driver(mca_adsp_driver);

MODULE_DESCRIPTION("Xiaomi MCA Qualcomm ADSP PMIC-GLINK transport");
MODULE_LICENSE("GPL v2");
