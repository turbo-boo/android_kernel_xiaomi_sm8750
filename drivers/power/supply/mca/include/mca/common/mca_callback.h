/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MCA_CALLBACK_H
#define MCA_CALLBACK_H

#include <linux/cleanup.h>
#include <linux/srcu.h>

struct mca_callback_scope {
	struct srcu_struct *srcu;
	int index;
};

/* Charging callbacks may sleep or invoke another MCA class. */
DEFINE_CLASS(mca_callback, struct mca_callback_scope,
	srcu_read_unlock(_T.srcu, _T.index),
	((struct mca_callback_scope){ srcu, srcu_read_lock(srcu) }),
	struct srcu_struct *srcu);

#endif
