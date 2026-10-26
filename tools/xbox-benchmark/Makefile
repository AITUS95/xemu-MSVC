# SPDX-License-Identifier: GPL-2.0-or-later
XBE_TITLE = Xbox\ Benchmark\ Pilot\ 2
GEN_XISO = xbox-benchmark.iso
SRCS = $(CURDIR)/main.c $(CURDIR)/kernels.c
NXDK_DIR ?= /opt/nxdk
$(CURDIR)/main.obj $(CURDIR)/kernels.obj: CFLAGS += -O2 -Wall -Wextra -Werror -fno-vectorize -fno-slp-vectorize
$(CURDIR)/extended.obj: CXXFLAGS += -Wall -Wextra -Werror
-include $(CURDIR)/generated.mk
include $(NXDK_DIR)/Makefile
