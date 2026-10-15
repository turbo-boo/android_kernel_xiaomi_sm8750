// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../../../drivers/power/supply/mca/include/mca/smartchg/basp_wire.h"

static unsigned char packet[MCA_BASP_MAX_SIZE];
static size_t offsets[3];
static unsigned int checks;

static void checksum(void)
{
	struct smart_basp_header *h = (void *)packet;
	unsigned int i;

	h->checksum = 0;
	for (i = 4; i < h->total_len; i++)
		h->checksum += packet[i];
}

static void valid_packet(void)
{
	struct smart_basp_header *h = (void *)packet;
	struct smart_batt_jeita_term_para term = { { 0, -65535, 65535 }, 4510, 100 };
	struct smart_batt_spec *spec;
	unsigned int i;

	memset(packet, 0, sizeof(packet));
	h->jeita_normal_term_size = 1;
	h->wired_ffc_size = 1;
	memcpy(packet + sizeof(*h), &term, sizeof(term));
	spec = (void *)(packet + sizeof(*h) + sizeof(term));
	spec->ffc = 1;
	spec->t_range.idx = 0;
	spec->t_range.min = -65535;
	spec->t_range.max = 65535;
	spec->step_size = MCA_BASP_MAX_STEPS;
	for (i = 0; i < spec->step_size; i++)
		spec->steps[i] = (struct smart_batt_spec_curve){ 4000 + 50 * i, 6000, 1000 };
	h->total_len = sizeof(*h) + sizeof(term) + sizeof(*spec) + sizeof(spec->steps[0]) * spec->step_size;
	checksum();
}

static void check(int expected)
{
	int ret = mca_basp_validate(packet, sizeof(packet), offsets);

	assert((ret == 0) == expected);
	checks++;
}

#define INVALID(change) do { valid_packet(); change; checksum(); check(0); } while (0)

int main(void)
{
	struct smart_basp_header *h = (void *)packet;
	struct smart_batt_jeita_term_para *term = (void *)(packet + 36);
	struct smart_batt_spec *spec = (void *)(packet + 56);
	unsigned char unaligned[MCA_BASP_MAX_SIZE + 1];
	uint32_t state = 0x12345678;
	size_t i, j;

	valid_packet(); check(1);
	assert(offsets[0] == 36 && offsets[1] == 56 && offsets[2] == h->total_len);
	memcpy(unaligned + 1, packet, sizeof(packet));
	assert(!mca_basp_validate(unaligned + 1, sizeof(packet), offsets)); checks++;
	for (i = 0; i < h->total_len; i++) {
		assert(mca_basp_validate(packet, i, offsets)); checks++;
	}
	assert(mca_basp_validate(NULL, sizeof(packet), offsets)); checks++;
	assert(mca_basp_validate(packet, sizeof(packet), NULL)); checks++;
	assert(mca_basp_validate(packet, sizeof(packet) + 1, offsets)); checks++;
	packet[4] ^= 1; check(0);
	INVALID(h->wired_ffc_size = UINT32_MAX);
	INVALID(term->t_range.idx = -1);
	INVALID(term->t_range.idx = MCA_BASP_MAX_ROWS);
	INVALID(term->t_range.min = -65536);
	INVALID(term->t_range.max = 65536);
	INVALID(term->t_range.min = term->t_range.max);
	INVALID(term->vterm = 0);
	INVALID(term->iterm = -1);
	INVALID(spec->step_size = 0);
	INVALID(spec->step_size = UINT32_MAX);
	INVALID(spec->ffc = 0);
	INVALID(spec->ffc = 2);
	INVALID(spec->t_range.idx = -1);
	INVALID(spec->t_range.idx = 15);
	INVALID(spec->t_range.min = spec->t_range.max);
	INVALID(spec->steps[0].mv = 0);
	INVALID(spec->steps[1].mv = spec->steps[0].mv);
	INVALID(spec->steps[2].mv = spec->steps[1].mv - 1);
	INVALID(spec->steps[0].ma_l = -1);
	INVALID(spec->steps[0].ma_h = spec->steps[0].ma_l - 1);
	INVALID(h->total_len++);
	INVALID(h->total_len--);
	/* Duplicate rows must not overwrite one another in the same table. */
	valid_packet();
	memmove(packet + 76, packet + 56, h->total_len - 56);
	memcpy(packet + 56, packet + 36, 20);
	h->total_len += 20;
	h->jeita_normal_term_size = 2;
	checksum(); check(0);
	/* Empty sections are legal; reject even one unaccounted payload byte. */
	memset(packet, 0, sizeof(packet)); h->total_len = 36; checksum(); check(1);
	h->total_len = 37; checksum(); check(0);
	/* Deterministic malformed packets exercise the same parser used in-kernel. */
	for (i = 0; i < 20000; i++) {
		for (j = 0; j < sizeof(packet); j++) {
			state ^= state << 13; state ^= state >> 17; state ^= state << 5;
			packet[j] = state;
		}
		h->total_len = 36 + i % (sizeof(packet) - 35);
		checksum();
		assert(mca_basp_validate(packet, sizeof(packet), offsets)); checks++;
	}
	printf("BASP wire: %u checks passed\n", checks);
	return 0;
}
