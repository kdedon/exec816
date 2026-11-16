# SPDX-License-Identifier: GPL-3.0-only
#
# Exec900: the Commodore 900 (Z8001) build of the Exec816 kernel.
# docs/internals.md describes the toolchain, layout and test contract.

SHELL = /bin/sh
.DELETE_ON_ERROR:
.SECONDARY:

C900	:= $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
REPO	:= $(abspath $(C900)/..)
OUT	?= $(REPO)/build/c900
OBJ	= $(OUT)/obj

include $(C900)/mk/deps.mk

# Kernel sources.  crt.o is linked first: kboot enters the image at its
# text start, where crt.s keeps the pad and the PSA.  execvec.o, generated
# from fd/exec_lib.fd, follows: crt.o has no data, so SysBase is the first
# long of the data segment.
KCSRC	= kernel/tasks.c kernel/signals.c kernel/ports.c kernel/semaphores.c \
	  kernel/memory.c kernel/pools.c kernel/boot.c kernel/kprintf.c platform/machine.c \
	  kernel/interrupts.c kernel/devices.c kernel/libraries.c kernel/loadseg.c \
	  kernel/bootmods.c devices/timer.c devices/wd.c \
	  devices/battclock.c devices/serial.c devices/parallel.c devices/beep.c devices/keyboard.c \
	  devices/console.c devices/conkeymap.c kernel/epu.c
KSSRC	= platform/crt.s platform/trap.s platform/mmu.s platform/block.s \
	  platform/lists.s platform/semfast.s platform/epuasm.s
KOBJ	= $(OBJ)/crt.o $(OBJ)/execvec.o $(OBJ)/trap.o $(OBJ)/mmu.o $(OBJ)/block.o $(OBJ)/lists.o $(OBJ)/semfast.o $(OBJ)/epuasm.o $(patsubst %.c,$(OBJ)/%.o,$(notdir $(KCSRC)))

# The test-only TestHooks table and port I/O, which programs link too.
KCSRC	+= kernel/testhooks.c
KSSRC	+= platform/portio.s
KOBJ	+= $(OBJ)/portio.o

# The smoke image needs no Task, memory or port code.
SMOKEOBJ = $(OBJ)/crt.o $(OBJ)/trap.o $(OBJ)/mmu.o $(OBJ)/machine.o $(OBJ)/kprintf.o \
	   $(OBJ)/lists.o $(OBJ)/portio.o $(OBJ)/t_smoke.o

# The root program of the default image: a tests/ name.  Every image
# boots the same exec900.sys and runs its root program from disk.
ROOT	?= hello
TESTS	= $(filter-out smoke,$(basename $(notdir $(wildcard $(C900)/tests/*.c))))

# Emulator budget per test, in instructions.
BUDGET	?= 400000000

ABIH	= $(OUT)/include/exec/abi.h $(patsubst %,$(OUT)/include/exec/abi/%.h,nodes tasks ports memory io errors)
LVOH	= $(OUT)/include/lvo/exec_lib.h $(OUT)/include/lvo/battclock_lib.h $(OUT)/include/lvo/timer_lib.h \
	  $(OUT)/include/lvo/framebuf_lib.h $(OUT)/include/lvo/gameport_lib.h \
	  $(OUT)/include/lvo/dos_lib.h \
	  $(OUT)/include/lvo/mathieeedoubbas_lib.h \
	  $(OUT)/include/lvo/mathieeedoubtrans_lib.h
HDRS	= $(wildcard $(C900)/include/exec/*.h) \
	  $(wildcard $(C900)/include/devices/*.h) \
	  $(wildcard $(C900)/include/resources/*.h) $(wildcard $(C900)/include/dos/*.h) $(wildcard $(C900)/include/utility/*.h) $(C900)/kernel/kernel.h \
	  $(C900)/kernel/testhooks.h \
	  $(C900)/platform/c900.h $(C900)/platform/display.h \
	  $(C900)/platform/vt.h $(ABIH) $(LVOH)
INC	= -I$(C900)/include -I$(OUT)/include -I$(C900)/kernel \
	  -I$(C900)/platform -I$(KBOOTSRC)/include

VAR	= 800000020800

vpath %.c $(C900)/kernel $(C900)/platform $(C900)/devices
vpath %.s $(C900)/platform

.PHONY: all smoke test check hostcheck clean help deps release
all: $(OUT)/exec900.sys $(OUT)/exec900.bin

help:
	@echo "make [all]        build $(OUT)/exec900.sys and exec900.bin (root: ROOT=$(ROOT))"
	@echo "make smoke        boot the console-only smoke image and check it"
	@echo "make test T=name  boot exec900.sys with tests/name.c as the root program, check PASS"
	@echo "make check        hostcheck, smoke and every tests/*.c"
	@echo "make hostcheck    host-side tool tests (tests/host)"
	@echo "make clean        remove $(OUT)"
	@echo "make release      pack the release archives into dist/ (TAG=v0.1.0)"
	@echo "make deps         unpack the releases ../DEPS lists into deps/ (DEP=name for one)"
	@echo "Inputs: C900_TOOLCHAIN=$(C900_TOOLCHAIN)"
	@echo "        KBOOT=$(KBOOT) KBOOTSRC=$(KBOOTSRC)"
	@echo "        C900_EMU=$(C900_EMU) BUDGET=$(BUDGET)"

clean:
	rm -rf $(OUT)

$(OBJ) $(OUT)/include/exec:
	mkdir -p $@

$(ABIH) &: $(C900)/tools/genabi.py $(wildcard $(REPO)/abi/*.json) | $(OUT)/include/exec
	python3 $(C900)/tools/genabi.py $(REPO)/abi $(OUT)/include

# exec.library's call macros and jump table; the base is a struct ExecBase.
GENLVO	= python3 $(C900)/tools/genlvo.py
# sizeof(struct ExecBase), <exec/execbase.h>; kernel/libraries.c checks it.
EXECBASESIZE = 158
$(OUT)/include/lvo/exec_lib.h $(OBJ)/execvec.s: $(C900)/fd/exec_lib.fd $(C900)/tools/genlvo.py | $(OBJ)
	$(GENLVO) $< $(OUT)/include/lvo/exec_lib.h $(OBJ)/execvec.s ExecLib_ $(EXECBASESIZE)

$(OUT)/include/lvo/%.h: $(C900)/fd/%.fd $(C900)/tools/genlvo.py
	$(GENLVO) $< $@

$(OBJ)/execvec.o: $(OBJ)/execvec.s
	$(AS) -g -o $@ $<

# cc0 -> cc1 -> cc2.  0012 relocates frame references against SS (0x3F).
define CCRULE
	$(CC0) $(VAR) $(1) $(OBJ)/$(2).z0 $(INC) $(4)
	$(CC1) $(VAR) $(OBJ)/$(2).z0 $(OBJ)/$(2).z1
	$(CC2) 0012 $(OBJ)/$(2).z1 $(3) $(OBJ)/$(2).scr 0
endef

# Kernel sources call the x functions directly; tests use the jump table.
$(OBJ)/%.o: %.c $(HDRS) | $(OBJ)
	$(call CCRULE,$<,$*,$@,-DEXEC_KERNEL -DEXECBASESIZE=$(EXECBASESIZE))

$(OBJ)/t_%.o: $(C900)/tests/%.c $(HDRS) $(wildcard $(C900)/tests/*.h) | $(OBJ)
	$(call CCRULE,$<,t_$*,$@)

$(OBJ)/%.o: %.s $(C900)/platform/c900.h | $(OBJ)
	cpp -traditional-cpp -P -I$(C900)/platform $< > $(OBJ)/$*.i
	$(AS) -g -o $@ $(OBJ)/$*.i

# Link, then check the segment sizes and print the layout (tools/loutsize.py).
define LINK
	$(LD) -i -L -e start -R 0x30000000 -o $(1) $(2)
	python3 $(C900)/tools/loutsize.py $(1)
endef

$(OUT)/exec900.sys: $(KOBJ)
	$(call LINK,$@,$^)

$(OUT)/smoke.sys: $(SMOKEOBJ)
	$(call LINK,$@,$^)

# Hunk files (tools/mkhunk.py): tests/hunks/NAME.c, linked with `ld -r'
# and the absolute symbols of progabs.o, becomes $(OUT)/hunks/NAME.
HUNKS	= $(patsubst %.c,$(OUT)/hunks/%,$(notdir $(wildcard $(C900)/tests/hunks/*.c)))

$(OBJ)/h_%.o: $(C900)/tests/hunks/%.c $(HDRS) | $(OBJ)
	$(call CCRULE,$<,h_$*,$@)

$(OUT)/hunks/%: $(OBJ)/h_%.o $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/hunks
	$(LD) -r -o $(OBJ)/h_$*.r $(OBJ)/h_$*.o $(HUNKOBJ_$*) $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py $(HUNKFLAGS_$*) $@ $(OBJ)/h_$*.r

HUNKFLAGS_seghunk = --entry=start

# dlt.library, tests/ramlib.c's disk library, as docs/platform.md's
# recipe builds one: its vectors (DltFuncs) and the callers' macros
# (<lvo/dlt_lib.h>) come from its fd file.
$(OUT)/include/lvo/dlt_lib.h $(OBJ)/dltvec.c: $(C900)/tests/hunks/dlt_lib.fd $(C900)/tools/genlvo.py | $(OBJ)
	$(GENLVO) $< $(OUT)/include/lvo/dlt_lib.h --table $(OBJ)/dltvec.c DltFuncs dt
$(OBJ)/dt_dltvec.o: $(OBJ)/dltvec.c | $(OBJ)
	$(call CCRULE,$<,dt_dltvec,$@)
HUNKOBJ_dlt = $(OBJ)/dt_dltvec.o
HUNKOBJ_dltlong = $(OBJ)/dt_dltvec.o
$(OUT)/hunks/dlt $(OUT)/hunks/dltlong: $(OBJ)/dt_dltvec.o
$(OBJ)/h_dltlong.o: $(C900)/tests/hunks/dlt.c
$(OBJ)/t_ramlib.o: $(OUT)/include/lvo/dlt_lib.h

# The fake intuition.library of tests/alertint.c and tests/crashint.c:
# DisplayAlert, the system requester calls and TimedDisplayAlert at
# intuition's vectors.
$(OUT)/include/lvo/fakeint_lib.h $(OBJ)/fakeintvec.c: $(C900)/tests/hunks/fakeint_lib.fd $(C900)/tools/genlvo.py | $(OBJ)
	$(GENLVO) $< $(OUT)/include/lvo/fakeint_lib.h --table $(OBJ)/fakeintvec.c FakeIFuncs fi
$(OBJ)/fi_fakeintvec.o: $(OBJ)/fakeintvec.c | $(OBJ)
	$(call CCRULE,$<,fi_fakeintvec,$@)
HUNKOBJ_fakeint = $(OBJ)/fi_fakeintvec.o
$(OUT)/hunks/fakeint: $(OBJ)/fi_fakeintvec.o
$(OBJ)/h_fakeint.o $(OBJ)/t_alertint.o $(OBJ)/t_crashint.o: $(C900)/tests/hunks/fakeint.h

# Programs: lib/startup.s first, so that start opens the code hunk, then
# the program, the program library (lib/) and progabs.o.  tests/NAME.c
# becomes $(OUT)/progs/NAME.
PLIB	= $(OBJ)/l_amiga.o $(OBJ)/portio.o
PROGS	= $(OUT)/progs

$(OBJ)/l_%.o: $(C900)/lib/%.c $(HDRS) | $(OBJ)
	$(call CCRULE,$<,l_$*,$@)

$(OBJ)/l_%.o: $(C900)/lib/%.s | $(OBJ)
	$(AS) -g -o $@ $<

$(PROGS)/%: $(OBJ)/l_startup.o $(OBJ)/t_%.o $(PLIB) $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(PROGS)
	$(LD) -r -o $(OBJ)/p_$*.r $(OBJ)/l_startup.o $(OBJ)/t_$*.o $(PLIB_$*) $(PLIB) $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py --entry=start $@ $(OBJ)/p_$*.r

# Display modules: textcon.device's display half, the terminal emulator
# and one card's backend, with the program library.  kbootmods loads the
# one for the card textcon.device found; every boot volume carries all
# three.
DISPCOMMON = $(OBJ)/m_condisp.o $(OBJ)/m_vt.o $(PLIB) $(OBJ)/block.o
DISPOBJ_lr = $(OBJ)/m_lr.o
DISPOBJ_hr = $(OBJ)/m_hr.o $(OBJ)/m_font8x16.o $(OBJ)/glyphs.o
DISPOBJ_lc = $(OBJ)/m_lc.o $(OBJ)/m_font8x8.o $(OBJ)/m_font8x16.o
DISPLAYS = $(patsubst %,$(OUT)/hunks/%.display,lr hr lc)
# gameport.device, loaded with hr.display (V7 names hold 14 bytes).
GAMEPORT = $(OUT)/hunks/gameport.dev
DISPDISK = $(patsubst %,--file=%,$(DISPLAYS) $(GAMEPORT))
HUNKS	+= $(DISPLAYS) $(GAMEPORT)

$(OBJ)/m_%.o: %.c $(HDRS) | $(OBJ)
	$(call CCRULE,$<,m_$*,$@)

define DISPRULE
$(OUT)/hunks/$(1).display: $(DISPCOMMON) $(DISPOBJ_$(1)) $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/hunks
	$(LD) -r -o $(OBJ)/d_$(1).r $(DISPCOMMON) $(DISPOBJ_$(1)) $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py $$@ $(OBJ)/d_$(1).r
endef
$(foreach d,lr hr lc,$(eval $(call DISPRULE,$(d))))

$(GAMEPORT): $(OBJ)/m_gameport.o $(PLIB) $(OBJ)/block.o $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/hunks
	$(LD) -r -o $(OBJ)/gameport.r $(OBJ)/m_gameport.o $(PLIB) $(OBJ)/block.o $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py $@ $(OBJ)/gameport.r

# lance.device, the LANCE card's SANA-II driver: a boot module listed in
# Modules (mkdisk.py --module).
LANCEDEV = $(OUT)/hunks/lance.device
HUNKS	+= $(LANCEDEV)

$(LANCEDEV): $(OBJ)/m_lance.o $(PLIB) $(OBJ)/block.o $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/hunks
	$(LD) -r -o $(OBJ)/lance.r $(OBJ)/m_lance.o $(PLIB) $(OBJ)/block.o $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py $@ $(OBJ)/lance.r

# dos.library (a boot module), its handlers (boot modules, started as
# processes by DOS), the shell (L:Shell-Seg) and the C: commands.  DOS
# programs start with lib/dstartup.s and lib/dosstart.c.
DOSDIR	= $(C900)/dos
DOSLIB	= $(patsubst %,$(OBJ)/dl_%.o,doslib dosmisc dospkt doslist dosio dosext \
	  dosbuf dosargs dospat dosvar dosdate dosproc ramlib doscrash dosvec dosasm) $(OBJ)/l_amiga.o
DOSPLIB	= $(OBJ)/l_dstartup.o
DOSRT	= $(OBJ)/l_dosstart.o $(OBJ)/l_amiga.o
DOSCMDS	= $(basename $(notdir $(wildcard $(DOSDIR)/c/*.c)))
DOSHUNKS = $(OUT)/hunks/dos.library $(OUT)/hunks/con-handler \
	   $(OUT)/hunks/v7fs-handler $(OUT)/hunks/ram-handler \
	   $(OUT)/hunks/Shell-Seg \
	   $(patsubst %,$(OUT)/c/%,$(DOSCMDS))
HUNKS	+= $(DOSHUNKS)

# Commands tests put in C: (tests/cmds/NAME.c, as $(OUT)/tc/NAME).
$(OBJ)/tc_%.o: $(C900)/tests/cmds/%.c $(HDRS) | $(OBJ)
	$(call CCRULE,$<,tc_$*,$@)

$(OUT)/tc/%: $(DOSPLIB) $(OBJ)/tc_%.o $(DOSRT) $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/tc
	$(LD) -r -o $(OBJ)/tcmd_$*.r $(DOSPLIB) $(OBJ)/tc_$*.o $(DOSRT) $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py --entry=start $@ $(OBJ)/tcmd_$*.r
HUNKS	+= $(patsubst %.c,$(OUT)/tc/%,$(notdir $(wildcard $(C900)/tests/cmds/*.c)))

# A DOS system volume: the modules, the shell and the C: commands.
DOSDISK	= --module=$(OUT)/hunks/dos.library --module=$(OUT)/hunks/con-handler \
	  --module=$(OUT)/hunks/v7fs-handler --module=$(OUT)/hunks/ram-handler \
	  --put=L/Shell-Seg=$(OUT)/hunks/Shell-Seg \
	  $(foreach c,$(DOSCMDS),--put=C/$(c)=$(OUT)/c/$(c))

# An 80,000-byte pattern file (byte i = i * 7 + i / 512), which reaches
# the double-indirect block.
# A 300-byte stand-in for an icon (byte i = i * 5).
$(OUT)/lnicon:
	python3 -c 'import sys; sys.stdout.buffer.write(bytes(i * 5 & 255 for i in range(300)))' > $@

$(OUT)/v7big:
	python3 -c 'import sys; sys.stdout.buffer.write(bytes((i * 7 + (i >> 9)) & 255 for i in range(80000)))' > $@

$(OUT)/include/lvo/dos_lib.h $(OBJ)/dosvec.c: $(C900)/fd/dos_lib.fd $(C900)/tools/genlvo.py | $(OBJ)
	$(GENLVO) $< $(OUT)/include/lvo/dos_lib.h --table $(OBJ)/dosvec.c DosFuncs dl

$(OBJ)/dl_dosvec.o: $(OBJ)/dosvec.c | $(OBJ)
	$(call CCRULE,$<,dl_dosvec,$@)

$(OBJ)/dl_%.o: $(DOSDIR)/%.c $(HDRS) $(DOSDIR)/dosint.h | $(OBJ)
	$(call CCRULE,$<,dl_$*,$@,-I$(DOSDIR))

$(OBJ)/dl_%.o: $(DOSDIR)/%.s | $(OBJ)
	$(AS) -g -o $@ $<

$(OBJ)/c_%.o: $(DOSDIR)/c/%.c $(HDRS) $(DOSDIR)/c/cargs.h | $(OBJ)
	$(call CCRULE,$<,c_$*,$@)

$(OUT)/hunks/dos.library: $(DOSLIB) $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/hunks
	$(LD) -r -o $(OBJ)/dos.r $(DOSLIB) $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py $@ $(OBJ)/dos.r

# A handler: lib/startup.s (its process entry), the handler, amiga.c.
$(OUT)/hunks/%-handler: $(OBJ)/l_startup.o $(OBJ)/dl_%.o $(OBJ)/l_amiga.o $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/hunks
	$(LD) -r -o $(OBJ)/h_$*.r $(OBJ)/l_startup.o $(OBJ)/dl_$*.o $(OBJ)/l_amiga.o $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py --entry=start $@ $(OBJ)/h_$*.r

$(OUT)/hunks/Shell-Seg: $(DOSPLIB) $(OBJ)/dl_shell.o $(DOSRT) $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/hunks
	$(LD) -r -o $(OBJ)/shell.r $(DOSPLIB) $(OBJ)/dl_shell.o $(DOSRT) $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py --entry=start $@ $(OBJ)/shell.r

$(OUT)/c/%: $(DOSPLIB) $(OBJ)/c_%.o $(DOSRT) $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/c
	$(LD) -r -o $(OBJ)/cmd_$*.r $(DOSPLIB) $(OBJ)/c_$*.o $(DOSRT) $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py --entry=start $@ $(OBJ)/cmd_$*.r

# mathieeedoubbas.library, the boot module ieeedoubbas (V7 names hold 14
# bytes): the software path on the C library's floating-point runtime,
# the EPU path in math/mathepu.s.
MATHLIB	= $(OUT)/hunks/ieeedoubbas
HUNKS	+= $(MATHLIB)

$(OBJ)/mbvec.c: $(C900)/fd/mathieeedoubbas_lib.fd $(C900)/tools/genlvo.py | $(OBJ)
	$(GENLVO) $< $(OUT)/include/lvo/mathieeedoubbas_lib.h --table $@ MbFuncs mb

$(OBJ)/mb_mbvec.o: $(OBJ)/mbvec.c | $(OBJ)
	$(call CCRULE,$<,mb_mbvec,$@)

$(OBJ)/mb_%.o: $(C900)/math/%.c $(HDRS) | $(OBJ)
	$(call CCRULE,$<,mb_$*,$@)

$(OBJ)/mb_%.o: $(C900)/math/%.s | $(OBJ)
	$(AS) -g -o $@ $<

# mathieeedoubtrans.library, the boot module ieeedoubtran, over the
# toolchain's libm (built as $(LIBM)).
MATHTRANS = $(OUT)/hunks/ieeedoubtran
HUNKS	+= $(MATHTRANS)
LIBM	= $(OUT)/lib/libm.a
LIBMOBJ	= $(patsubst %.c,$(OBJ)/lm/%.o,$(notdir $(wildcard $(C900_TOOLCHAIN)/src/libm/*.c)))

$(OBJ)/lm:
	mkdir -p $@
$(OBJ)/lm/%.o: $(C900_TOOLCHAIN)/src/libm/%.c $(LIBCH) | $(OBJ)/lm
	$(call CCRULE,$<,lm/$*,$@,$(CINC))
$(LIBM): $(LIBMOBJ)
	@mkdir -p $(dir $@)
	@rm -f $@
	$(MKARZ) $@ $^

$(OBJ)/mtvec.c: $(C900)/fd/mathieeedoubtrans_lib.fd $(C900)/tools/genlvo.py | $(OBJ)
	$(GENLVO) $< $(OUT)/include/lvo/mathieeedoubtrans_lib.h --table $@ MtFuncs mt

$(OBJ)/mb_mtvec.o: $(OBJ)/mtvec.c | $(OBJ)
	$(call CCRULE,$<,mb_mtvec,$@)

$(MATHTRANS): $(OBJ)/mb_mathieeedoubtrans.o $(OBJ)/mb_mtvec.o $(OBJ)/mb_mathepu.o $(LIBM) $(OUT)/lib/libc.a $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/hunks
	$(LD) -r -o $(OBJ)/mathtrans.r $(OBJ)/mb_mathieeedoubtrans.o $(OBJ)/mb_mtvec.o $(OBJ)/mb_mathepu.o $(LIBM) $(LIBC) $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py $@ $(OBJ)/mathtrans.r

$(MATHLIB): $(OBJ)/mb_mathieeedoubbas.o $(OBJ)/mb_mbvec.o $(OBJ)/mb_mathepu.o $(OUT)/lib/libc.a $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	@mkdir -p $(OUT)/hunks
	$(LD) -r -o $(OBJ)/mathbas.r $(OBJ)/mb_mathieeedoubbas.o $(OBJ)/mb_mbvec.o $(OBJ)/mb_mathepu.o $(LIBC) $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py $@ $(OBJ)/mathbas.r

# The C library (docs/platform.md, C library): the toolchain's MWC
# COHERENT libc sources, compiled here for the stack segment, with the
# system-call layer and startup replaced by libc/*.c over dos.library.
# A C program links libc/crt0.s first, its objects, $(LIBC) and progabs.o.
TCLIBC	= $(C900_TOOLCHAIN)/src/libc
TCINC	= $(C900_TOOLCHAIN)/src/include
LIBC	= $(OUT)/lib/libc.a
LIBCDIR	= $(C900)/libc
# Toolchain sources libc/*.c replaces, that need what Exec900 lacks
# (profiling), or that are stubs of something the library has.
LIBCSKIP = gen/_finish gen/sdtoa gen/malloc gen/realloc gen/notmem \
	   gen/isatty gen/mkdir gen/system gen/getwd gen/getcwd gen/sleep \
	   gen/monitor gen/abort gen/getenv gen/strerror gen/perror crt/_prof \
	   stdio/exit sys/sbrk \
	   $(FPSOFT)
# The software double add, multiply and divide, renamed fs*: libc/fpepu.s
# takes their names and calls them when no EPU is fitted.
FPSOFT	= crt/dadd crt/dmul crt/ddiv
LIBCTC	= $(filter-out $(addprefix $(TCLIBC)/,$(addsuffix .c,$(LIBCSKIP)) \
	  $(addsuffix .s,$(LIBCSKIP))),$(wildcard $(TCLIBC)/gen/*.c \
	  $(TCLIBC)/stdio/*.c $(TCLIBC)/crt/*.c $(TCLIBC)/sys/*.c \
	  $(TCLIBC)/gen/*.s $(TCLIBC)/crt/*.s))
LIBCOBJ	= $(patsubst %,$(OBJ)/lc/%.o,$(basename $(notdir $(LIBCTC)))) \
	  $(patsubst %.c,$(OBJ)/lx_%.o,$(notdir $(wildcard $(LIBCDIR)/*.c))) \
	  $(patsubst crt/%,$(OBJ)/lc/fs_%.o,$(FPSOFT)) $(OBJ)/lx_fpepu.o
LIBCH	= $(wildcard $(LIBCDIR)/*.h $(LIBCDIR)/include/*.h)
# A C program's headers: Exec900's errno.h, then exec and DOS, then the
# toolchain's (<include/errno.h> reaches the original).
CINC	= -I$(LIBCDIR)/include -I$(C900)/include -I$(OUT)/include \
	  -I$(TCINC) -I$(C900_TOOLCHAIN)/src
LIBCRT	= $(OBJ)/lx_crt0.o

$(OBJ)/lc $(OUT)/lib:
	mkdir -p $@

$(OBJ)/lc/%.o: $(TCLIBC)/gen/%.c $(LIBCH) | $(OBJ)/lc
	$(call CCRULE,$<,lc/$*,$@,$(CINC) -I$(TCINC)/sys)
$(OBJ)/lc/%.o: $(TCLIBC)/stdio/%.c $(LIBCH) | $(OBJ)/lc
	$(call CCRULE,$<,lc/$*,$@,$(CINC) -I$(TCINC)/sys)
$(OBJ)/lc/%.o: $(TCLIBC)/crt/%.c $(LIBCH) | $(OBJ)/lc
	$(call CCRULE,$<,lc/$*,$@,$(CINC) -I$(TCINC)/sys)
$(OBJ)/lc/%.o: $(TCLIBC)/sys/%.c $(LIBCH) | $(OBJ)/lc
	$(call CCRULE,$<,lc/$*,$@,$(CINC) -I$(TCINC)/sys)
$(OBJ)/lc/%.o: $(TCLIBC)/gen/%.s | $(OBJ)/lc
	$(AS) -o $@ $<
$(OBJ)/lc/%.o: $(TCLIBC)/crt/%.s | $(OBJ)/lc
	$(AS) -o $@ $<

$(OBJ)/lx_%.o: $(LIBCDIR)/%.c $(HDRS) $(LIBCH) | $(OBJ)
	$(call CCRULE,$<,lx_$*,$@,$(CINC))
$(OBJ)/lx_crt0.o: $(LIBCDIR)/crt0.s | $(OBJ)
	$(AS) -g -o $@ $<
$(OBJ)/lx_fpepu.o: $(LIBCDIR)/fpepu.s | $(OBJ)
	$(AS) -g -o $@ $<
$(OBJ)/lc/fs_%.o: $(TCLIBC)/crt/%.s | $(OBJ)/lc
	sed -E 's/\<(d[rl](add|sub|mul|div))\>/fs\1/g' $< > $(OBJ)/lc/fs_$*.s
	$(AS) -o $@ $(OBJ)/lc/fs_$*.s

# Linked ahead of $(LIBC), nofloat.o replaces printf's floating-point
# conversions (%e, %f, %g) with a stub that reports them and exits.
$(OUT)/lib/nofloat.o: $(TCLIBC)/gen/sdtoa.c $(LIBCH) | $(OBJ)/lc $(OUT)/lib
	$(call CCRULE,$<,lc/sdtoa,$@,$(CINC) -I$(TCINC)/sys)

$(LIBC): $(LIBCOBJ)
	@mkdir -p $(dir $@)
	@rm -f $@
	@echo "mkarz $@ ($(words $^) members)"
	@$(MKARZ) $@ $^

# CLINK(out, objects): a C program as a hunk file.
define CLINK
	@mkdir -p $(dir $(1))
	$(LD) -r -o $(1).r $(LIBCRT) $(2) $(LIBC) $(OBJ)/progabs.o
	python3 $(C900)/tools/mkhunk.py --entry=start $(1) $(1).r
	@rm -f $(1).r
endef

# C test commands (tests/libc/NAME.c, as $(OUT)/tl/NAME).
$(OBJ)/tl_%.o: $(C900)/tests/libc/%.c $(LIBCH) | $(OBJ)
	$(call CCRULE,$<,tl_$*,$@,$(CINC))

$(OUT)/tl/%: $(OBJ)/tl_%.o $(LIBCRT) $(LIBC) $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	$(call CLINK,$@,$<)
HUNKS	+= $(patsubst %.c,$(OUT)/tl/%,$(notdir $(wildcard $(C900)/tests/libc/*.c)))

# Hunk files over 64 KB (tests/bighunk.c): tests/split/genbig.py writes
# the parts, and mkhunk.py --split links them in groups of one hunk each;
# bigprog's libc members are a group of their own.
SPLIT	= $(OUT)/split
SPLITPARTS = $(patsubst %,$(SPLIT)/big%.o,0 1 2)
SPLITMK	= python3 $(C900)/tools/mkhunk.py --split --ld=$(LD)

$(SPLIT)/bigexp.h: $(C900)/tests/split/genbig.py
	python3 $< $(SPLIT)
$(SPLIT)/big%.c: $(SPLIT)/bigexp.h ;

$(SPLIT)/big%.o: $(SPLIT)/big%.c
	$(call CCRULE,$<,../split/big$*,$@)

$(OBJ)/sp_%.o: $(C900)/tests/split/%.c $(HDRS) | $(OBJ)
	$(call CCRULE,$<,sp_$*,$@)

$(SPLIT)/bigprog: $(OBJ)/sp_bigprog.o $(SPLITPARTS) $(LIBC) $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	$(SPLITMK) --entry=start $@ $(filter %.o %.a,$^)

$(SPLIT)/biglib: $(OBJ)/sp_biglib.o $(SPLITPARTS) $(OBJ)/progabs.o $(C900)/tools/mkhunk.py
	$(SPLITMK) $@ $(filter %.o,$^)

$(OBJ)/t_bighunk.o: $(C900)/tests/bighunk.c $(SPLIT)/bigexp.h $(HDRS) $(wildcard $(C900)/tests/*.h) | $(OBJ)
	$(call CCRULE,$<,t_bighunk,$@,-I$(SPLIT))

# make cprog SRC=prog.c [NOFLOAT=1]: any C program, as $(OUT)/cprog/prog.
CPROG	= $(OUT)/cprog/$(basename $(notdir $(SRC)))
$(OBJ)/cp_%.o: $(SRC) $(LIBCH) | $(OBJ)
	$(call CCRULE,$<,cp_$*,$@,$(CINC))

.PHONY: libc cprog
libc: $(LIBC) $(LIBCRT) $(OUT)/lib/nofloat.o

cprog: $(LIBC) $(LIBCRT) $(OBJ)/progabs.o $(OUT)/lib/nofloat.o
	@test -n "$(SRC)" || { echo "make cprog: name the source, SRC=prog.c" >&2; exit 2; }
	$(MAKE) -f $(C900)/Makefile $(OBJ)/cp_$(basename $(notdir $(SRC))).o SRC=$(abspath $(SRC))
	$(call CLINK,$(CPROG),$(OBJ)/cp_$(basename $(notdir $(SRC))).o $(if $(NOFLOAT),$(OUT)/lib/nofloat.o))

# Tests that drive the terminal emulator directly link it.
PLIB_vt = $(OBJ)/m_vt.o
PLIB_perf = $(OBJ)/m_vt.o
$(PROGS)/vt $(PROGS)/perf: $(OBJ)/m_vt.o
# tests/epu.c: its EPU helpers and the C library's floating-point runtime.
$(OBJ)/ts_%.o: $(C900)/tests/%.s | $(OBJ)
	$(AS) -g -o $@ $<
PLIB_epu = $(OBJ)/ts_epu.o $(LIBC)
$(PROGS)/epu: $(OBJ)/ts_epu.o $(LIBC)

# Boot-volume contents beyond the kernel, per image (mkdisk.py).
DISK_bootmod	= --module=$(OUT)/hunks/tmod
DISK_loadseg	= --file=$(OUT)/hunks/seghunk
DISK_lcdisp400	= --lcmode=400
$(OBJ)/t_lcdisp400.o: $(C900)/tests/lcdisp.c
DISK_bighunk	= --file=$(SPLIT)/bigprog --file=$(SPLIT)/biglib
$(OUT)/bighunk.bin: $(SPLIT)/bigprog $(SPLIT)/biglib
DISK_dosproc	= --module=$(OUT)/hunks/dos.library
DISK_dosdate	= --module=$(OUT)/hunks/dos.library
DISK_v7fs	= --module=$(OUT)/hunks/dos.library \
		  --module=$(OUT)/hunks/v7fs-handler \
		  --put=Devs/Data/big=$(OUT)/v7big --put=L/seghunk=$(OUT)/hunks/seghunk
$(OUT)/v7fs.bin: $(OUT)/v7big
DISK_dosboot	= $(DOSDISK) --put=C/DosPass=$(OUT)/tc/DosPass \
		  --put=S/Startup-Sequence=$(C900)/tests/sys/Startup-Sequence \
		  --put=S/Motd=$(C900)/tests/sys/Motd \
		  --put=Prefs/Env-Archive/Language=$(C900)/tests/sys/Language
$(OBJ)/t_dosboot.o: $(DOSDIR)/dosboot.c
$(OUT)/dosboot.bin: $(C900)/tests/sys/Startup-Sequence $(C900)/tests/sys/Motd \
	$(C900)/tests/sys/Language
DISK_shell	= $(DOSDISK) --put=C/RetCode=$(OUT)/tc/RetCode \
		  --put=C/KPut=$(OUT)/tc/KPut \
		  --put=S/Fail=$(C900)/tests/sys/Fail
$(OUT)/shell.bin: $(C900)/tests/sys/Fail
SHELLX	= ShellX ShellIn ShellQ ShellQ2 ShellF ShellR ShellW ShellN Fail
DISK_shellx	= $(DOSDISK) --put=C/RetCode=$(OUT)/tc/RetCode \
		  --put=C/KPut=$(OUT)/tc/KPut --put=C/StkSize=$(OUT)/tc/StkSize \
		  $(foreach s,$(SHELLX),--put=S/$(s)=$(C900)/tests/sys/$(s))
$(OUT)/shellx.bin: $(patsubst %,$(C900)/tests/sys/%,$(SHELLX))
DISK_libc	= $(DOSDISK) --put=C/CTest=$(OUT)/tl/CTest \
		  --put=C/CHello=$(OUT)/tl/CHello \
		  --put=S/CScript=$(C900)/tests/sys/CScript
$(OUT)/libc.bin: $(C900)/tests/sys/CScript
DISK_fswrite	= --module=$(OUT)/hunks/dos.library \
		  --module=$(OUT)/hunks/v7fs-handler --put=Devs/Data/big=$(OUT)/v7big
$(OUT)/fswrite.bin: $(OUT)/v7big
DISK_fsbench	= --module=$(OUT)/hunks/dos.library --module=$(OUT)/hunks/v7fs-handler
DISK_fscrash	= $(DISK_fsbench)
DISK_lnames	= --module=$(OUT)/hunks/dos.library \
		  --module=$(OUT)/hunks/v7fs-handler --lnames \
		  --put=C/Seghunk-with-a-long-name.30=$(OUT)/hunks/seghunk \
		  --put=Prefs/Calculator.info=$(OUT)/lnicon
$(OUT)/lnames.bin: $(OUT)/lnicon
DISK_v7share	= --module=$(OUT)/hunks/dos.library \
		  --module=$(OUT)/hunks/v7fs-handler --lnames --sysbuffers=8
DISK_ramfs	= --module=$(OUT)/hunks/dos.library --module=$(OUT)/hunks/ram-handler
DISK_relocs	= $(DISK_ramfs)
DISK_conhand	= --module=$(OUT)/hunks/dos.library --module=$(OUT)/hunks/con-handler
DISK_conwin	= $(DOSDISK)
DISK_sermouse	= --module=$(GAMEPORT)
DISK_lancenone	= --module=$(LANCEDEV)
DISK_lanloop	= --module=$(LANCEDEV)
DISK_lanping	= --module=$(LANCEDEV)
DISK_dosbuf	= --module=$(OUT)/hunks/dos.library --module=$(OUT)/hunks/v7fs-handler
DISK_dosargs	= $(DISK_dosbuf)
DISK_dospat	= $(DISK_dosbuf)
DISK_dosvar	= $(DISK_dosbuf)
DISK_cmdargs	= $(DOSDISK)
DISK_taskstacks	= $(DOSDISK)
DISK_stkcfg	= --stack=48
DISK_tailmem	= --stack=32
DISK_rootstk	= --rootstack=1
DISK_rootstk45	= --stack=45 --rootstack=1
DISK_epu	= --module=$(MATHLIB) --module=$(MATHTRANS)
DISK_alertint	= --module=$(OUT)/hunks/dos.library \
		  --module=$(OUT)/hunks/v7fs-handler \
		  --put=Libs/intuition.library=$(OUT)/hunks/fakeint
DISK_crashnoint	= --module=$(OUT)/hunks/dos.library
DISK_crashforbid = $(DISK_crashnoint)
DISK_crashdisable = $(DISK_crashnoint)
DISK_crashirq	= $(DISK_crashnoint)
DISK_crashint	= --module=$(OUT)/hunks/dos.library \
		  --module=$(OUT)/hunks/v7fs-handler \
		  --put=Libs/intuition.library=$(OUT)/hunks/fakeint
DISK_ramlib	= --module=$(OUT)/hunks/dos.library \
		  --module=$(OUT)/hunks/v7fs-handler --module=$(OUT)/hunks/tmod \
		  --put=Libs/dlt.library=$(OUT)/hunks/dlt \
		  --put=Libs/dltlongname.library=$(OUT)/hunks/dltlong \
		  --put=Libs/dlfail.library=$(OUT)/hunks/dlfail \
		  --put=Libs/dlnest.library=$(OUT)/hunks/dlnest \
		  --put=Libs/tmod2.library=$(OUT)/hunks/tmod \
		  --put=Devs/dlt.device=$(OUT)/hunks/dltdev

# A test whose subject is the kernel image's layout boots its own image:
# exec900.sys's objects plus tests/kimage/NAME.c, as $(OUT)/NAME.sys.
KIMAGES	= $(basename $(notdir $(wildcard $(C900)/tests/kimage/*.c)))
KERNEL	= $(if $(filter $(1),$(KIMAGES)),$(OUT)/$(1).sys,$(OUT)/exec900.sys)

$(OBJ)/k_%.o: $(C900)/tests/kimage/%.c $(HDRS) | $(OBJ)
	$(call CCRULE,$<,k_$*,$@)

$(patsubst %,$(OUT)/%.sys,$(KIMAGES)): $(OUT)/%.sys: $(KOBJ) $(OBJ)/k_%.o
	$(call LINK,$@,$^)

# MKDISK(image, root, extra): the kernel with root as the root program.
MKDISK	= python3 $(C900)/tools/mkdisk.py --kboot=$(KBOOT) --root=$(PROGS)/$(2) \
	  $(DISK_$(2)) $(DISPDISK) $(3) $(1) $(call KERNEL,$(2))
DISKDEPS = $(C900)/tools/mkdisk.py $(KBOOT) $(HUNKS)

$(OUT)/exec900.bin: $(DISKDEPS) $(OUT)/exec900.sys $(PROGS)/$(ROOT)
	$(call MKDISK,$@,$(ROOT))

.SECONDEXPANSION:
$(OUT)/%.bin: $(DISKDEPS) $$(call KERNEL,$$*) $(PROGS)/%
	$(call MKDISK,$@,$*)

$(OUT)/smoke.bin: $(OUT)/smoke.sys $(C900)/tools/mkdisk.py $(KBOOT)
	python3 $(C900)/tools/mkdisk.py --kboot=$(KBOOT) $@ $<

RUNTEST	= sh $(C900)/tools/runtest.sh $(C900_EMU) $(BUDGET)

smoke: $(OUT)/smoke.bin
	$(RUNTEST) smoke $(OUT)/smoke.bin $(OUT)/smoke.log

test:
	@test -n "$(T)" || { echo "make test: name a test, e.g. T=lists" >&2; exit 2; }
	@test "$(T)" != smoke || { echo "make test: smoke has its own target: make smoke" >&2; exit 2; }
	@test -f $(C900)/tests/$(T).c || { echo "make test: no tests/$(T).c" >&2; exit 2; }
	$(MAKE) -f $(C900)/Makefile $(OUT)/$(T).bin
	$(RUNTEST) $(T) $(OUT)/$(T).bin $(OUT)/$(T).log $(C900)/tests/$(T).c

deps:
	sh $(C900)/tools/deps-fetch.sh $(DEP)

# The release archives (tools/pack-release.sh), e.g. TAG=v0.1.0.
release: all $(HUNKS) $(PROGS)/dosboot
	@test -n "$(TAG)" || { echo "make release: name a tag, e.g. TAG=v0.1.0" >&2; exit 2; }
	C900_TOOLCHAIN=$(C900_TOOLCHAIN) KBOOTSRC=$(KBOOTSRC) \
	  sh $(C900)/tools/pack-release.sh $(TAG) $(OUT)

hostcheck:
	python3 -B -m unittest discover -s $(C900)/tests/host -p 'test_*.py'

check: hostcheck
	$(MAKE) -f $(C900)/Makefile $(patsubst %,$(OUT)/%.bin,smoke $(TESTS))
	@fail=0; \
	for t in smoke $(TESTS); do \
		if [ $$t = smoke ]; then \
			$(MAKE) -f $(C900)/Makefile smoke || fail=1; \
		else \
			$(MAKE) -f $(C900)/Makefile test T=$$t || fail=1; \
		fi; \
	done; \
	[ $$fail = 0 ] && echo "check: all passed" || { echo "check: FAILED" >&2; exit 1; }

# Device-complete targets: simtest, simcheck, rtltest, rtlcheck (docs/testing.md).
include $(C900)/tools/bench.mk
