#ifndef _MCA_COMMON_MCA_WORKQUEUE_H_
#define _MCA_COMMON_MCA_WORKQUEUE_H_

#include <linux/cleanup.h>
#include <linux/list.h>
#include <linux/workqueue.h>

struct task_struct;

struct mca_work_callback {
	struct list_head node;
	struct work_struct *work;
	struct task_struct *task;
};

struct mca_work_callback *mca_work_callback_enter(
	struct mca_work_callback *callback, struct work_struct *work);
void mca_work_callback_exit(struct mca_work_callback *callback);

/* Track the stack-owned callback without importing non-KMI current_work(). */
DEFINE_CLASS(mca_work_callback, struct mca_work_callback *,
	mca_work_callback_exit(_T), mca_work_callback_enter(callback, work),
	struct mca_work_callback *callback, struct work_struct *work);

int mca_queue_delayed_work(struct delayed_work *dwork, unsigned long delay);
int mca_mod_delayed_work(struct delayed_work *dwork, unsigned long delay);
int mca_queue_work(struct work_struct *work);
int mca_cancel_work(struct work_struct *work);
int mca_cancel_delayed_work(struct delayed_work *dwork);

void mca_cancel_delayed_work_sync(struct delayed_work *dwork);

#endif /* _MCA_COMMON_MCA_WORKQUEUE_H_ */
