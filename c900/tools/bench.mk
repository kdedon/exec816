# SPDX-License-Identifier: GPL-3.0-only
#
# Device-complete test targets: the C900 Go machine model (sim) and the
# C900_MiSTer whole-core Verilator bench (rtl).  docs/testing.md describes
# them.  Included by the Makefile; uses its OUT, C900, GITDIR, TESTS and
# disk-image rules (MKDISK, KERNEL, PROGS, DISKDEPS).

C900SIM		?= $(GITDIR)/C900/sim
Z8000SIM	?= $(GITDIR)/z8000/sim
C900_MISTER	?= $(GITDIR)/C900_MiSTer
CARD		?= none
BENCHJOBS	?= 4
BENCH		= $(OUT)/bench

SIMRUN		= $(OUT)/tools/simrun
TBCORE		= $(OUT)/rtl/tb_core
TBROM		= $(OUT)/rtl/boot.rom
TBPATCH		?= $(C900_MISTER)/tb/core/skip_memtest.patch

RUNBENCH	= SIMRUN=$(SIMRUN) TBCORE=$(TBCORE) TBROM=$(TBROM) TBPATCH=$(TBPATCH) \
		  sh $(C900)/tools/runbench.sh

.PHONY: simrun tbcore simtest simcheck rtltest rtlcheck

# go builds incrementally; the workspace joins simrun to the model's modules.
simrun:
	@test -d $(C900SIM)/machine || { echo "no C900 model at $(C900SIM): set C900SIM" >&2; exit 2; }
	mkdir -p $(OUT)/tools
	rm -f $(OUT)/tools/go.work
	cd $(OUT)/tools && go work init $(C900)/tools/simrun $(C900SIM) $(Z8000SIM)
	cp $(C900SIM)/../go.work.sum $(OUT)/tools/ 2>/dev/null || true
	cd $(C900)/tools/simrun && GOWORK=$(OUT)/tools/go.work go build -o $(SIMRUN) .

tbcore:
	@test -f $(C900_MISTER)/tb/core/tb_core.sv || { echo "no bench at $(C900_MISTER): set C900_MISTER" >&2; exit 2; }
	mkdir -p $(OUT)/rtl
	$(MAKE) -C $(C900_MISTER)/tb/core -f $(C900)/tools/rtl.mk MDIR=$(OUT)/rtl
	test -f $(TBROM) || $(C900_MISTER)/scripts/make_boot_rom.sh $(GITDIR)/C900/firmware $(TBROM)

# The core bench holds at most 21 MB of hard disk and treats an image of
# any other drive size as no drive, so its images are drive type 1.
RTLIMG		= $(OUT)/rtlimg
RTLGEOM		= 612,4,17,306
BENCHIMG	= $(if $(filter rtl,$(1)),$(RTLIMG),$(OUT))/$(T).bin

$(RTLIMG)/smoke.bin: $(OUT)/smoke.sys $(C900)/tools/mkdisk.py $(KBOOT)
	mkdir -p $(RTLIMG)
	python3 $(C900)/tools/mkdisk.py --kboot=$(KBOOT) --geom=$(RTLGEOM) $@ $<

$(RTLIMG)/%.bin: $(DISKDEPS) $$(call KERNEL,$$*) $(PROGS)/%
	mkdir -p $(RTLIMG)
	$(call MKDISK,$@,$*,--geom=$(RTLGEOM))

# BENCHTEST(target): boot $(T) on that target with $(CARD).
define BENCHTEST
	@test -n "$(T)" || { echo "make $(1)test: name a test, e.g. T=hello" >&2; exit 2; }
	@test "$(T)" = smoke || test -f $(C900)/tests/$(T).c || { echo "make $(1)test: no tests/$(T).c" >&2; exit 2; }
	$(MAKE) -f $(C900)/Makefile $(call BENCHIMG,$(1))
	mkdir -p $(BENCH)
	$(RUNBENCH) $(1) $(CARD) $(T) $(call BENCHIMG,$(1)) $(BENCH)/$(T).$(1)-$(CARD).log \
		$(if $(filter-out smoke,$(T)),$(C900)/tests/$(T).c)
endef

# BENCHCHECK(target): smoke and every test, BENCHJOBS at a time.
define BENCHCHECK
	$(MAKE) -f $(C900)/Makefile $(patsubst %,$(if $(filter rtl,$(1)),$(RTLIMG),$(OUT))/%.bin,smoke $(TESTS))
	@printf '%s\n' smoke $(TESTS) | \
	    xargs -P $(BENCHJOBS) -I{} $(MAKE) -s --no-print-directory -f $(C900)/Makefile \
		$(1)test T={} CARD=$(CARD) BENCHBUILT=1 && echo "$(1)check ($(CARD)): all passed" || \
	    { echo "$(1)check ($(CARD)): FAILED" >&2; exit 1; }
endef

# The check targets build the tools once, then run tests with BENCHBUILT set.
simtest: $(if $(BENCHBUILT),,simrun)
	$(call BENCHTEST,sim)

simcheck: simrun
	$(call BENCHCHECK,sim)

rtltest: $(if $(BENCHBUILT),,tbcore)
	$(call BENCHTEST,rtl)

rtlcheck: tbcore
	$(call BENCHCHECK,rtl)

.PHONY: benchhelp
benchhelp:
	@echo "make simtest T=name [CARD=none|lr|hr]       boot a test in the C900 Go model"
	@echo "make simcheck [CARD=..] [BENCHJOBS=$(BENCHJOBS)]       smoke plus every test there"
	@echo "make rtltest T=name [CARD=none|lr|hr|lc]    boot a test on the MiSTer core bench"
	@echo "make rtlcheck [CARD=..] [BENCHJOBS=$(BENCHJOBS)]       smoke plus every test there"
	@echo "Logs and screens: $(BENCH)/<test>.<target>-<card>.log[.screen.*]"
	@echo "Inputs: C900SIM=$(C900SIM) Z8000SIM=$(Z8000SIM)"
	@echo "        C900_MISTER=$(C900_MISTER) TBPATCH=$(TBPATCH)"
