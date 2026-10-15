#ifndef _MCA_COMMON_MCA_VOTER_H_
#define _MCA_COMMON_MCA_VOTER_H_

#include <linux/types.h>

struct mca_votable;

enum mca_votable_type {
	MCA_VOTE_MIN,
	MCA_VOTE_MAX,
	MCA_VOTE_OR,
	MCA_VOTE_AND,
	NUM_VOTABLE_TYPES,
};

void mca_lock_votable(struct mca_votable *votable);
void mca_unlock_votable(struct mca_votable *votable);
bool mca_is_override_vote_enabled_locked(struct mca_votable *votable);
bool mca_is_override_vote_enabled(struct mca_votable *votable);
bool mca_is_client_vote_enabled_locked(struct mca_votable *votable,
				       const char *client_str);
bool mca_is_client_vote_enabled(struct mca_votable *votable,
				const char *client_str);
int mca_get_client_vote_locked(struct mca_votable *votable,
			       const char *client_str);
int mca_get_client_vote(struct mca_votable *votable, const char *client_str);
int mca_get_effective_result_locked(struct mca_votable *votable);
int mca_get_effective_result(struct mca_votable *votable);
const char *mca_get_effective_client_locked(struct mca_votable *votable);
const char *mca_get_effective_client(struct mca_votable *votable);
int mca_vote(struct mca_votable *votable, const char *client_str,
	     bool enabled, int val);
int mca_vote_override(struct mca_votable *votable,
		      const char *override_client, bool enabled, int val);
int mca_rerun_election(struct mca_votable *votable);
struct mca_votable *mca_find_votable(const char *name);
struct mca_votable *mca_create_votable(
	const char *name, int votable_type,
	int (*callback)(struct mca_votable *votable, void *data,
			int effective_result, const char *effective_client),
	int default_value, void *data);
void mca_destroy_votable(struct mca_votable *votable);
void mca_release_votables(void *data);

#endif /* _MCA_COMMON_MCA_VOTER_H_ */
