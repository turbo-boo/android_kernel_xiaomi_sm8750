/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MCA_BASP_WIRE_H
#define MCA_BASP_WIRE_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <stddef.h>
#include <string.h>
#endif

#define MCA_BASP_MAX_SIZE 4096
#define MCA_BASP_MAX_ROWS 15
#define MCA_BASP_MAX_STEPS 8

struct smart_batt_spec_curve { int mv, ma_h, ma_l; };
struct smart_batt_jeita_term_para {
	struct { int idx, min, max; } t_range;
	int vterm, iterm;
};
/* Dada serializes curves inline, never as a userspace pointer. */
struct smart_batt_spec {
	unsigned int type, ffc;
	struct { int idx, min, max; } t_range;
	unsigned int step_size;
	struct smart_batt_spec_curve steps[];
};
struct smart_basp_header {
	unsigned int checksum, type, total_len;
	unsigned int jeita_ffc_term_size, jeita_normal_term_size;
	unsigned int wired_ffc_size, wired_normal_size;
	unsigned int wls_ffc_size, wls_normal_size;
};

_Static_assert(sizeof(struct smart_basp_header) == 36, "Dada BASP header");
_Static_assert(offsetof(struct smart_batt_spec, steps) == 24, "Dada BASP curve offset");
_Static_assert(sizeof(struct smart_batt_jeita_term_para) == 20, "Dada BASP JEITA record");

static inline int mca_basp_range_valid(int idx, int min, int max)
{
	return idx >= 0 && idx < MCA_BASP_MAX_ROWS && min >= -65535 &&
	       max <= 65535 && min < max;
}

/* Validate the complete transaction before invoking any charging consumer. */
static inline int mca_basp_validate(const void *buffer, size_t size,
				    size_t offsets[3])
{
	const unsigned char *bytes = buffer;
	struct smart_basp_header h;
	unsigned int counts[6], sum = 0, group, row;
	size_t offset = sizeof(h), i;

	if (!buffer || !offsets || size < sizeof(h) || size > MCA_BASP_MAX_SIZE)
		return -EINVAL;
	memcpy(&h, bytes, sizeof(h));
	if (h.total_len < sizeof(h) || h.total_len > size)
		return -EINVAL;
	for (i = sizeof(h.checksum); i < h.total_len; i++)
		sum += bytes[i];
	if (sum != h.checksum)
		return -EBADMSG;
	counts[0] = h.jeita_ffc_term_size;
	counts[1] = h.jeita_normal_term_size;
	counts[2] = h.wired_ffc_size;
	counts[3] = h.wired_normal_size;
	counts[4] = h.wls_ffc_size;
	counts[5] = h.wls_normal_size;
	for (group = 0; group < 6; group++) {
		unsigned int seen = 0;

		if (counts[group] > MCA_BASP_MAX_ROWS)
			return -EINVAL;
		if (!(group & 1))
			offsets[group / 2] = offset;
		for (row = 0; row < counts[group]; row++) {
			int idx;

			if (group < 2) {
				struct smart_batt_jeita_term_para term;

				if (sizeof(term) > h.total_len - offset)
					return -EINVAL;
				memcpy(&term, bytes + offset, sizeof(term));
				if (!mca_basp_range_valid(term.t_range.idx,
					term.t_range.min, term.t_range.max) ||
				    term.vterm <= 0 || term.iterm < 0)
					return -EINVAL;
				idx = term.t_range.idx;
				offset += sizeof(term);
			} else {
				/* Aligned local storage also handles unaligned wire input. */
				union {
					struct smart_batt_spec spec;
					unsigned char storage[24 + 12 * MCA_BASP_MAX_STEPS];
				} record;
				struct smart_batt_spec *spec = &record.spec;
				size_t len;
				unsigned int step;

				if (sizeof(*spec) > h.total_len - offset)
					return -EINVAL;
				memcpy(spec, bytes + offset, sizeof(*spec));
				if (!spec->step_size || spec->step_size > MCA_BASP_MAX_STEPS ||
				    spec->ffc != !(group & 1) ||
				    !mca_basp_range_valid(spec->t_range.idx,
					 spec->t_range.min, spec->t_range.max))
					return -EINVAL;
				len = sizeof(*spec) + spec->step_size * sizeof(spec->steps[0]);
				if (len > h.total_len - offset)
					return -EINVAL;
				memcpy(record.storage, bytes + offset, len);
				for (step = 0; step < spec->step_size; step++) {
					const struct smart_batt_spec_curve *c = &spec->steps[step];

					if (c->mv <= 0 || c->ma_l < 0 || c->ma_h < c->ma_l ||
					    (step && c->mv <= spec->steps[step - 1].mv))
						return -EINVAL;
				}
				idx = spec->t_range.idx;
				offset += len;
			}
			if (seen & (1U << idx))
				return -EINVAL;
			seen |= 1U << idx;
		}
	}
	return offset == h.total_len ? 0 : -EINVAL;
}
#endif
