# SPDX-License-Identifier: GPL-3.0-only
#
# External inputs.  Each defaults to the release in deps/ (make deps), else
# a sibling checkout of exec816, and can be overridden on the command line or
# in the environment.

GITDIR		:= $(abspath $(C900)/../..)
DEPDIR		= $(or $(wildcard $(C900)/deps/$(1)),$(GITDIR)/$(1))
C900_TOOLCHAIN	?= $(call DEPDIR,commodore-900-toolchain)
KBOOTSRC	?= $(call DEPDIR,commodore-900-kboot)
KBOOT		?= $(firstword $(wildcard $(KBOOTSRC)/kboot) $(KBOOTSRC)/build/kboot)
C900_EMU	?= $(call DEPDIR,commodore-900-emulator)

TC	= $(C900_TOOLCHAIN)/host/build
CC0	= $(TC)/z8001/cc0-z8001
CC1	= $(TC)/z8001/cc1-z8001
CC2	= $(TC)/z8001/cc2-z8001
AS	= $(TC)/as-z8001
LD	= $(TC)/ld-z8001
LOUTDIS	= $(TC)/tools/loutdis
MKARZ	= $(TC)/mkarz

# Refuse early, by name, when an input is missing.  clean, help and deps need none.
NEEDS	= $(filter-out clean help deps,$(or $(MAKECMDGOALS),all))
ifneq ($(strip $(NEEDS)),)
$(foreach t,$(CC0) $(CC1) $(CC2) $(AS) $(LD) $(MKARZ),$(if $(wildcard $(t)),,\
  $(error no $(t): build commodore-900-toolchain or set C900_TOOLCHAIN)))
ifeq ($(wildcard $(KBOOTSRC)/include/bootinfo.h),)
$(error no $(KBOOTSRC)/include/bootinfo.h: check out commodore-900-kboot or set KBOOTSRC)
endif
ifeq ($(wildcard $(KBOOT)),)
$(error no kboot at $(KBOOT): build commodore-900-kboot or set KBOOT)
endif
ifneq ($(filter smoke test check,$(NEEDS)),)
ifeq ($(wildcard $(C900_EMU)/bin/c900),)
$(error no emulator at $(C900_EMU)/bin/c900: build commodore-900-emulator or set C900_EMU)
endif
endif
endif
