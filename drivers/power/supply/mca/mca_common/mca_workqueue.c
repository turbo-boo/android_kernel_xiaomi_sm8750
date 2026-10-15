// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA workqueue core.
 * Reconstructed from the public Onyx MCA implementation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_workqueue.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_workqueue"
#endif

static struct workqueue_struct *mca_wq;
static LIST_HEAD(mca_active_work);
static DEFINE_SPINLOCK(mca_active_work_lock);

struct mca_work_callback *mca_work_callback_enter(
	struct mca_work_callback *callback, struct work_struct *work)
{
	unsigned long flags;

	callback->work = work;
	callback->task = current;
	spin_lock_irqsave(&mca_active_work_lock, flags);
	list_add(&callback->node, &mca_active_work);
	spin_unlock_irqrestore(&mca_active_work_lock, flags);
	return callback;
}
EXPORT_SYMBOL(mca_work_callback_enter);

void mca_work_callback_exit(struct mca_work_callback *callback)
{
	unsigned long flags;

	spin_lock_irqsave(&mca_active_work_lock, flags);
	list_del(&callback->node);
	spin_unlock_irqrestore(&mca_active_work_lock, flags);
}
EXPORT_SYMBOL(mca_work_callback_exit);

void mca_cancel_delayed_work_sync(struct delayed_work *dwork)
{
	struct mca_work_callback *callback;
	unsigned long flags;
	bool self = false;

	spin_lock_irqsave(&mca_active_work_lock, flags);
	list_for_each_entry(callback, &mca_active_work, node) {
		if (callback->work == &dwork->work && callback->task == current) {
			self = true;
			break;
		}
	}
	spin_unlock_irqrestore(&mca_active_work_lock, flags);
	/* A force-stop voter may run from the monitor it is stopping. */
	if (self)
		cancel_delayed_work(dwork);
	else
		cancel_delayed_work_sync(dwork);
}
EXPORT_SYMBOL(mca_cancel_delayed_work_sync);

int mca_queue_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work(mca_wq, dwork, delay);
}
EXPORT_SYMBOL(mca_queue_delayed_work);

int mca_mod_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
	return mod_delayed_work(mca_wq, dwork, delay);
}
EXPORT_SYMBOL(mca_mod_delayed_work);

int mca_queue_work(struct work_struct *work)
{
	return queue_work(mca_wq, work);
}
EXPORT_SYMBOL(mca_queue_work);

int mca_cancel_work(struct work_struct *work)
{
	return cancel_work(work);
}
EXPORT_SYMBOL(mca_cancel_work);

int mca_cancel_delayed_work(struct delayed_work *dwork)
{
	return cancel_delayed_work(dwork);
}
EXPORT_SYMBOL(mca_cancel_delayed_work);

static int __init mca_workqueue_init(void)
{
	mca_wq = alloc_workqueue("mca_wq",
		WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_CPU_INTENSIVE, 0);
	if (!mca_wq) {
		mca_log_err("alloc_workqueue failed\n");
		return -ENOMEM;
	}

	mca_log_info("MCA workqueue initialized\n");
	return 0;
}

static void __exit mca_workqueue_exit(void)
{
	if (mca_wq)
		destroy_workqueue(mca_wq);
}

module_init(mca_workqueue_init);
module_exit(mca_workqueue_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Xiaomi MCA workqueue core");
