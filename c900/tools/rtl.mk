# SPDX-License-Identifier: GPL-3.0-only
#
# Build the C900_MiSTer whole-core bench (tb_core) into a private
# directory, with the bench's own sources and Verilator flags.
#   make -C <C900_MiSTer>/tb/core -f rtl.mk MDIR=<dir>
# The working directory must be tb/core: the bench's source lists are
# relative to it.  The GHDL and VDC netlists the source lists name are
# made by their own rules in the MiSTer tree's obj_dir.

ROOT := ../..
include $(ROOT)/sources.mk

VERILATOR ?= verilator
MDIR ?= $(error MDIR is required)

VFLAGS := --cc --exe --build +1364-2005ext+v -O3 --x-assign fast --x-initial unique \
	  -Wno-fatal -Wno-lint -Wno-style --timescale 1ns/1ps -j 8 \
	  --top-module tb_core -o tb_core --Mdir $(MDIR) $(HR_INC) $(ROOT)/lint/vendor.vlt

TB_SRCS := $(ROOT)/tb/core/ssmon.sv $(wildcard $(ROOT)/rtl/host/uart_sel.sv) \
	   $(SDRAM_SIM_SRCS) $(VDC_SIM_SRCS) \
	   $(ROOT)/tb/common/sdram_cycle_model.sv $(ROOT)/rtl/host/centronics_printer.sv \
	   $(wildcard $(ROOT)/rtl/host/lance_link.sv $(ROOT)/rtl/host/boot_rom_load.sv)
TB_CPP := $(CURDIR)/tb_core.cpp $(wildcard $(CURDIR)/netpeer.cpp)

.PHONY: all
all: $(MDIR)/tb_core

$(MDIR)/tb_core: tb_core.sv $(TB_CPP) $(VENDOR_SRCS) $(CORE_SRCS) $(TB_SRCS)
	$(VERILATOR) $(VFLAGS) $(VENDOR_SRCS) $(CORE_SRCS) $(TB_SRCS) tb_core.sv $(TB_CPP)
