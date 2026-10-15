// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi MCA votable core for Dada.
 *
 * API and election semantics are reconstructed from Xiaomi's public Onyx MCA
 * implementation. Debugfs-only controls are deliberately left out of this
 * first Dada bring-up so the charging policy core can be validated first.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_voter"
#endif

#define MCA_MAX_VOTERS 32
#define MCA_CLIENT_NAME_MAX 50

struct mca_client_vote {
	bool enabled;
	int value;
};

struct mca_votable {
	char *name;
	char override_client[MCA_CLIENT_NAME_MAX];
	struct list_head list;
	struct mca_client_vote votes[MCA_MAX_VOTERS];
	char *client_names[MCA_MAX_VOTERS];
	int num_clients;
	int type;
	int effective_client_id;
	int effective_result;
	int override_result;
	int default_value;
	bool voted_on;
	struct mutex lock;
	void *data;
	int (*callback)(struct mca_votable *votable, void *data,
			int effective_result, const char *effective_client);
};

static DEFINE_SPINLOCK(mca_votable_list_lock);
static LIST_HEAD(mca_votable_list);
static DEFINE_MUTEX(mca_votable_create_lock);

void mca_lock_votable(struct mca_votable *votable)
{
	if (votable)
		mutex_lock(&votable->lock);
}
EXPORT_SYMBOL(mca_lock_votable);

void mca_unlock_votable(struct mca_votable *votable)
{
	if (votable)
		mutex_unlock(&votable->lock);
}
EXPORT_SYMBOL(mca_unlock_votable);

static int mca_find_client_id(struct mca_votable *votable,
			      const char *client)
{
	int i;

	for (i = 0; i < votable->num_clients; i++)
		if (votable->client_names[i] &&
		    !strcmp(votable->client_names[i], client))
			return i;

	return -ENOENT;
}

static int mca_get_or_add_client_id(struct mca_votable *votable,
				    const char *client)
{
	int id;

	id = mca_find_client_id(votable, client);
	if (id >= 0)
		return id;

	if (votable->num_clients >= MCA_MAX_VOTERS)
		return -ENOSPC;

	id = votable->num_clients;
	votable->client_names[id] = kstrdup(client, GFP_KERNEL);
	if (!votable->client_names[id])
		return -ENOMEM;

	votable->num_clients++;
	return id;
}

static const char *mca_client_name(struct mca_votable *votable, int id)
{
	if (!votable || id < 0 || id >= votable->num_clients)
		return NULL;
	return votable->client_names[id];
}

static void mca_elect(struct mca_votable *votable, int trigger_id,
		      int *result, int *winner)
{
	int i;

	*winner = -EINVAL;

	switch (votable->type) {
	case MCA_VOTE_MIN:
		*result = INT_MAX;
		for (i = 0; i < votable->num_clients; i++) {
			if (votable->votes[i].enabled &&
			    votable->votes[i].value < *result) {
				*result = votable->votes[i].value;
				*winner = i;
			}
		}
		break;
	case MCA_VOTE_MAX:
		*result = INT_MIN;
		for (i = 0; i < votable->num_clients; i++) {
			if (votable->votes[i].enabled &&
			    votable->votes[i].value > *result) {
				*result = votable->votes[i].value;
				*winner = i;
			}
		}
		break;
	case MCA_VOTE_OR:
		*result = 0;
		for (i = 0; i < votable->num_clients; i++) {
			if (!votable->votes[i].enabled)
				continue;
			*winner = trigger_id;
			*result = votable->votes[i].value;
			if (votable->votes[i].value)
				break;
		}
		break;
	case MCA_VOTE_AND:
		*result = 0;
		for (i = 0; i < votable->num_clients; i++) {
			if (!votable->votes[i].enabled)
				continue;
			*winner = trigger_id;
			*result = votable->votes[i].value;
			if (!votable->votes[i].value)
				break;
		}
		break;
	default:
		*result = votable->default_value;
		return;
	}

	if (*winner == -EINVAL)
		*result = votable->default_value;
}

bool mca_is_override_vote_enabled_locked(struct mca_votable *votable)
{
	return votable && votable->override_result != -EINVAL;
}
EXPORT_SYMBOL(mca_is_override_vote_enabled_locked);

bool mca_is_override_vote_enabled(struct mca_votable *votable)
{
	bool enabled;

	if (!votable)
		return false;
	mca_lock_votable(votable);
	enabled = mca_is_override_vote_enabled_locked(votable);
	mca_unlock_votable(votable);
	return enabled;
}
EXPORT_SYMBOL(mca_is_override_vote_enabled);

bool mca_is_client_vote_enabled_locked(struct mca_votable *votable,
				       const char *client_str)
{
	int id;

	if (!votable || !client_str)
		return false;
	id = mca_get_or_add_client_id(votable, client_str);
	return id >= 0 && votable->votes[id].enabled;
}
EXPORT_SYMBOL(mca_is_client_vote_enabled_locked);

bool mca_is_client_vote_enabled(struct mca_votable *votable,
				const char *client_str)
{
	bool enabled;

	if (!votable || !client_str)
		return false;
	mca_lock_votable(votable);
	enabled = mca_is_client_vote_enabled_locked(votable, client_str);
	mca_unlock_votable(votable);
	return enabled;
}
EXPORT_SYMBOL(mca_is_client_vote_enabled);

int mca_get_client_vote_locked(struct mca_votable *votable,
			       const char *client_str)
{
	int id;

	if (!votable || !client_str)
		return -EINVAL;
	id = mca_get_or_add_client_id(votable, client_str);
	if (id < 0)
		return -EINVAL;
	if (!votable->votes[id].enabled && votable->type != MCA_VOTE_OR)
		return -EINVAL;
	return votable->votes[id].value;
}
EXPORT_SYMBOL(mca_get_client_vote_locked);

int mca_get_client_vote(struct mca_votable *votable, const char *client_str)
{
	int value;

	if (!votable || !client_str)
		return -EINVAL;
	mca_lock_votable(votable);
	value = mca_get_client_vote_locked(votable, client_str);
	mca_unlock_votable(votable);
	return value;
}
EXPORT_SYMBOL(mca_get_client_vote);

int mca_get_effective_result_locked(struct mca_votable *votable)
{
	if (!votable)
		return -EINVAL;
	if (votable->override_result != -EINVAL)
		return votable->override_result;
	if (votable->effective_result == -EINVAL)
		return votable->default_value;
	return votable->effective_result;
}
EXPORT_SYMBOL(mca_get_effective_result_locked);

int mca_get_effective_result(struct mca_votable *votable)
{
	int value;

	if (!votable)
		return -EINVAL;
	mca_lock_votable(votable);
	value = mca_get_effective_result_locked(votable);
	mca_unlock_votable(votable);
	return value;
}
EXPORT_SYMBOL(mca_get_effective_result);

const char *mca_get_effective_client_locked(struct mca_votable *votable)
{
	if (!votable)
		return NULL;
	if (votable->override_result != -EINVAL)
		return votable->override_client;
	return mca_client_name(votable, votable->effective_client_id);
}
EXPORT_SYMBOL(mca_get_effective_client_locked);

const char *mca_get_effective_client(struct mca_votable *votable)
{
	const char *client;

	if (!votable)
		return NULL;
	mca_lock_votable(votable);
	client = mca_get_effective_client_locked(votable);
	mca_unlock_votable(votable);
	return client;
}
EXPORT_SYMBOL(mca_get_effective_client);

int mca_vote(struct mca_votable *votable, const char *client_str,
	     bool enabled, int val)
{
	int id, result, winner, rc = 0;
	bool changed;

	if (!votable || !client_str)
		return -EINVAL;

	mca_lock_votable(votable);
	id = mca_get_or_add_client_id(votable, client_str);
	if (id < 0) {
		rc = id;
		goto out;
	}

	votable->votes[id].enabled = enabled;
	votable->votes[id].value = val;
	mca_elect(votable, id, &result, &winner);
	changed = !votable->voted_on || result != votable->effective_result ||
		  winner != votable->effective_client_id;
	votable->effective_result = result;
	votable->effective_client_id = winner;
	votable->voted_on = true;

	if (changed && votable->callback && votable->override_result == -EINVAL)
		rc = votable->callback(votable, votable->data, result,
				       mca_client_name(votable, winner));
out:
	mca_unlock_votable(votable);
	return rc;
}
EXPORT_SYMBOL(mca_vote);

int mca_vote_override(struct mca_votable *votable,
		      const char *override_client, bool enabled, int val)
{
	int rc = 0;

	if (!votable || !override_client)
		return -EINVAL;

	mca_lock_votable(votable);
	if (enabled) {
		if (votable->callback)
			rc = votable->callback(votable, votable->data, val,
					       override_client);
		if (!rc) {
			strscpy(votable->override_client, override_client,
				MCA_CLIENT_NAME_MAX);
			votable->override_result = val;
		}
	} else {
		votable->override_result = -EINVAL;
		votable->override_client[0] = '\0';
		if (votable->callback)
			rc = votable->callback(votable, votable->data,
					       votable->effective_result,
					       mca_client_name(votable,
						votable->effective_client_id));
	}
	mca_unlock_votable(votable);
	return rc;
}
EXPORT_SYMBOL(mca_vote_override);

int mca_rerun_election(struct mca_votable *votable)
{
	int rc = 0;

	if (!votable)
		return -EINVAL;
	mca_lock_votable(votable);
	if (votable->callback)
		rc = votable->callback(votable, votable->data,
				       mca_get_effective_result_locked(votable),
				       mca_get_effective_client_locked(votable));
	mca_unlock_votable(votable);
	return rc;
}
EXPORT_SYMBOL(mca_rerun_election);

struct mca_votable *mca_find_votable(const char *name)
{
	struct mca_votable *votable;
	struct mca_votable *found = NULL;
	unsigned long flags;

	if (!name)
		return NULL;
	spin_lock_irqsave(&mca_votable_list_lock, flags);
	list_for_each_entry(votable, &mca_votable_list, list) {
		if (!strcmp(votable->name, name)) {
			found = votable;
			break;
		}
	}
	spin_unlock_irqrestore(&mca_votable_list_lock, flags);
	return found;
}
EXPORT_SYMBOL(mca_find_votable);

struct mca_votable *mca_create_votable(
	const char *name, int votable_type,
	int (*callback)(struct mca_votable *votable, void *data,
			int effective_result, const char *effective_client),
	int default_value, void *data)
{
	guard(mutex)(&mca_votable_create_lock);
	struct mca_votable *votable;
	unsigned long flags;

	if (!name || votable_type < 0 || votable_type >= NUM_VOTABLE_TYPES)
		return ERR_PTR(-EINVAL);

	votable = mca_find_votable(name);
	if (votable) {
		mca_lock_votable(votable);
		if (votable->data && votable->data != data) {
			mca_unlock_votable(votable);
			return ERR_PTR(-EBUSY);
		}
		if (votable->type != votable_type) {
			mca_unlock_votable(votable);
			return ERR_PTR(-EINVAL);
		}
		votable->callback = callback;
		votable->data = data;
		votable->default_value = default_value;
		mca_unlock_votable(votable);
		return votable;
	}

	votable = kzalloc(sizeof(*votable), GFP_KERNEL);
	if (!votable)
		return ERR_PTR(-ENOMEM);
	votable->name = kstrdup(name, GFP_KERNEL);
	if (!votable->name) {
		kfree(votable);
		return ERR_PTR(-ENOMEM);
	}

	mutex_init(&votable->lock);
	votable->type = votable_type;
	votable->callback = callback;
	votable->data = data;
	votable->default_value = default_value;
	votable->effective_result = votable_type == MCA_VOTE_OR ? 0 : -EINVAL;
	votable->effective_client_id = -EINVAL;
	votable->override_result = -EINVAL;

	spin_lock_irqsave(&mca_votable_list_lock, flags);
	list_add_tail(&votable->list, &mca_votable_list);
	spin_unlock_irqrestore(&mca_votable_list_lock, flags);
	return votable;
}
EXPORT_SYMBOL(mca_create_votable);

void mca_destroy_votable(struct mca_votable *votable)
{
	if (!votable)
		return;
	/* Consumers cache these addresses. Keep the registry entry for rebind. */
	mca_lock_votable(votable);
	votable->callback = NULL;
	votable->data = NULL;
	mca_unlock_votable(votable);
}
EXPORT_SYMBOL(mca_destroy_votable);

void mca_release_votables(void *data)
{
	struct list_head *node;
	unsigned long flags;

	if (!data)
		return;
	spin_lock_irqsave(&mca_votable_list_lock, flags);
	node = mca_votable_list.next;
	while (node != &mca_votable_list) {
		struct mca_votable *votable = list_entry(node, struct mca_votable, list);

		node = node->next;
		/* Registry nodes are stable; never hold a spinlock over a callback lock. */
		spin_unlock_irqrestore(&mca_votable_list_lock, flags);
		mca_lock_votable(votable);
		if (votable->data == data) {
			votable->callback = NULL;
			votable->data = NULL;
		}
		mca_unlock_votable(votable);
		spin_lock_irqsave(&mca_votable_list_lock, flags);
	}
	spin_unlock_irqrestore(&mca_votable_list_lock, flags);
}
EXPORT_SYMBOL(mca_release_votables);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Xiaomi MCA votable core");
