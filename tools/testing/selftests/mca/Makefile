# SPDX-License-Identifier: GPL-2.0
CFLAGS += -Wall -Wextra -Werror
TEST_GEN_PROGS := jeita_policy_test basp_wire_test
TEST_PROGS := wire_contract_test.py
TEST_FILES := dada_jeita_tables.h verify_stock_contract.py \
	dada_stock_contract.json dada_stock_gki_crc.json
LOCAL_HDRS += dada_jeita_tables.h \
	../../../../drivers/power/supply/mca/mca_strategy/strategy_jeita/mca_jeita_policy.h \
	../../../../drivers/power/supply/mca/include/mca/smartchg/basp_wire.h
include ../lib.mk
