#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# runbench.sh TARGET CARD NAME IMAGE LOG [SOURCE]
#
# Boot IMAGE on a device-complete test target and apply runtest.sh's
# contract: pass iff the console transcript LOG (CRs stripped) holds a
# line "PASS NAME" (or SOURCE's EXPECT text) and no FAIL line.
#
#   TARGET  sim  the C900 Go machine model, driven by tools/simrun
#           rtl  the C900_MiSTer whole-core Verilator bench (tb_core)
#   CARD    none, lr, hr (sim and rtl) or lc (rtl only)
#
# The display at the end of the run is saved next to LOG as LOG.screen.*.
# SOURCE's EMUFLAGS directive is translated where the target models the
# option (--ram=08-35; sim also --rtc=TIME, --input, --input-mark); a test
# needing any other emulator option is reported as SKIP.  FLOPPY: pattern
# attaches a fresh pattern floppy (LOG.fd) to drive 0; WIRE and SEND are
# skipped.
# PRINTER: text attaches a printer (sim: LOG.prn) and also requires its
# output to contain text; rtl skips such a test.
# KEYS: TICK:CODE,... injects keyboard bytes on sim and is skipped on rtl.
# BOOTS (several boots of one image) is skipped.
# MOUSE: TICK:DX:DY[:LRM],... moves the HR mouse on sim with CARD=hr, is
# ignored on sim without it and skipped on rtl.
# LANCE: loop fits the LANCE card (sim -lan, rtl +lance); LANCE: peer
# also puts a host at 10.0.9.1 on its cable (sim -lanpeer, pcap in
# LOG.pcap) and is skipped on rtl, whose peer has no ICMP.
# Environment:
#   SIMRUN     simrun binary (sim)
#   SIMSC      simrun -shortcut list (default hdc)
#   SIMBUDGET  simrun tick budget, 24 MHz ticks (default 1200000000)
#   SIMIDLE    give up after this many ticks without a console byte
#              (default 150000000)
#   SIMARGS    more simrun options, e.g. -key=TICK:CODE or -mouse=...
#   TBCORE     tb_core binary (rtl)
#   TBROM      48 KB boot ROM image (rtl)
#   TBPATCH    ROM patch (rtl; default the bench's skip_memtest.patch)
#   RTLMAX     Z8001 clock budget (rtl; default 120000000)
#   RTLIDLE    stop this many Z8001 clocks after the last console byte
#              (rtl; default 30000000, five seconds at 6 MHz)
#   RTLARGS    more tb_core plusargs, e.g. +key=N +keyat=CLOCK or +speed=3

if [ $# -ne 5 ] && [ $# -ne 6 ]; then
	echo "usage: runbench.sh sim|rtl none|lr|hr|lc NAME IMAGE LOG [SOURCE]" >&2
	exit 2
fi
target=$1 card=$2 name=$3 img=$4 log=$5 src=${6:-}

directive() {
	[ -n "$src" ] || return 0
	tr -d '\r' <"$src" |
	    sed -n "s|^/\* $1: *\(.*[^ ]\) *\*/\$|\1|p" | head -n 1
}

# \^X in a directive is the control byte X & 0x1F (not ^I or ^J, which
# would split the words): scripted input for line editing.
ctlbytes() {
	python3 -c 'import re, sys; sys.stdout.write(re.sub(r"\\\^([@-_])", lambda m: chr(ord(m.group(1)) - 64), sys.stdin.read()))'
}
expect=$(directive EXPECT)

# Translate the emulator options a test needs.  A test needing one the
# target does not model is skipped, not failed.
skip=
set --
for f in $(directive EMUFLAGS | ctlbytes); do
	case $target:$f in
	*:--ram=08-17) ;;
	sim:--ram=08-35) set -- "$@" -ramcard=3M ;;
	rtl:--ram=08-35) set -- "$@" +ram=2 ;;
	sim:--rtc=[0-9]*T*) set -- "$@" "-rtc=$(echo "${f#--rtc=}" | tr T ' ')" ;;
	sim:--input=*) set -- "$@" "-input=${f#--input=}" ;;
	sim:--input-mark=*) set -- "$@" "-input-mark=${f#--input-mark=}" ;;
	*) skip="$skip $f" ;;
	esac
done
case $(directive FLOPPY) in
pattern)
	python3 "$(dirname "$0")/mkdisk.py" --floppy "$log.fd" || exit 1
	case $target in
	sim) set -- "$@" -fd="$log.fd" ;;
	rtl) set -- "$@" +fd0="$log.fd" ;;
	esac
	;;
esac
wire=$(directive WIRE)
[ -z "$wire" ] || skip="$skip WIRE: $wire"
[ -z "$(directive SEND)" ] || skip="$skip SEND"
printed=$(directive PRINTER)
if [ -n "$printed" ]; then
	case $target in
	sim) rm -f "$log.prn"; set -- "$@" -printer="$log.prn" ;;
	*) skip="$skip PRINTER" ;;
	esac
fi
# BOOTS (several boots of one image) needs a writable disk.
[ -n "$(directive BOOTS)" ] && skip="$skip BOOTS"
# KEYS: TICK:CODE,... injects XT scancodes (simrun -key).
keys=$(directive KEYS)
if [ -n "$keys" ]; then
	case $target in
	sim) set -- "$@" -key="$keys" ;;
	*) skip="$skip KEYS" ;;
	esac
fi
# MOUSE: TICK:DX:DY[:LRM],... moves the HR mouse (simrun -mouse).
mouse=$(directive MOUSE)
if [ -n "$mouse" ]; then
	case $target:$card in
	sim:hr) set -- "$@" -mouse="$mouse" ;;
	sim:*) ;;
	*) skip="$skip MOUSE" ;;
	esac
fi
case $target:$(directive LANCE) in
*:) ;;
sim:loop) set -- "$@" -lan ;;
sim:peer) set -- "$@" -lan -lanpeer=10.0.9.1 -lanlog="$log.pcap" ;;
rtl:loop) set -- "$@" +lance ;;
*) skip="$skip LANCE: $(directive LANCE)" ;;
esac
if [ -n "$skip" ]; then
	echo "SKIP $name ($target, card $card): not modelled:$skip"
	exit 0
fi

rm -f "$log.screen".*
start=$(date +%s)
stop=
case $target in
sim)
	case $card in
	none|lr|hr) ;;
	*) echo "runbench: the sim has no '$card' card" >&2; exit 2 ;;
	esac
	shot=
	case $card in
	lr) shot=-screen="$log.screen.txt" ;;
	hr) shot=-screen="$log.screen.pbm" ;;
	esac
	"${SIMRUN:?SIMRUN is not set}" -hd="$img" -board="$card" \
		-shortcut="${SIMSC-hdc}" -max="${SIMBUDGET:-1200000000}" \
		-idle="${SIMIDLE:-150000000}" \
		-log="$log.raw" $shot $SIMARGS "$@" </dev/null 2>"$log.err"
	rc=$?
	;;
rtl)
	case $card in
	none) cardarg=+nocard shot= ;;
	lr) cardarg= shot=+screen ;;
	hr) cardarg=+hr shot=+hrvram="$log.screen.pbm" ;;
	lc) cardarg=+lc shot= ;;
	*) echo "runbench: no card '$card'" >&2; exit 2 ;;
	esac
	# tb_core serves the image as hps_io does, write-back included.
	cp --sparse=always "$img" "$log.img" || exit 1
	# Line-buffered, so $log.tb shows progress while the run lasts.
	stdbuf -oL "${TBCORE:?TBCORE is not set}" +rom="${TBROM:?TBROM is not set}" \
		${TBPATCH:++patch=$TBPATCH} $cardarg $shot +hd="$log.img" \
		+maxcyc="${RTLMAX:-120000000}" +idle="${RTLIDLE:-30000000}" $RTLARGS "$@" \
		</dev/null >"$log.tb" 2>"$log.err"
	rc=$?
	rm -f "$log.img"
	stop=$(grep '^stop:' "$log.tb" | tail -n 1)
	# Rebuild the transcript from the bench's SCC U74 channel B lines.
	sed -n "s/^SCC B @[0-9]*: \([0-9a-f][0-9a-f]\) .*/\1/p" "$log.tb" |
	    xxd -r -p >"$log.raw"
	if [ "$card" = lr ]; then
		sed -n 's/^[0-9][0-9]|\(.*\)|$/\1/p' "$log.tb" |
		    sed 's/ *$//' >"$log.screen.txt"
	fi
	;;
*)
	echo "runbench: unknown target '$target'" >&2
	exit 2
	;;
esac
end=$(date +%s)
tr -d '\r' <"$log.raw" >"$log"
rm -f "$log.raw"

status=ok
if [ "$target" = sim ] && [ $rc -eq 3 ]; then
	status="the run exhausted its tick budget without stopping"
elif [ "$target" = sim ] && [ $rc -eq 4 ]; then
	status="no console output for SIMIDLE ticks"
elif [ $rc -ne 0 ]; then
	status="$target exit status $rc"
elif grep -q '^FAIL' "$log"; then
	status="FAIL line"
elif [ -n "$expect" ]; then
	grep -qF -- "$expect" "$log" || status="no '$expect' line"
elif ! grep -q "^PASS $name\$" "$log"; then
	status="no 'PASS $name' line"
elif [ -n "$printed" ] && ! grep -qF -- "$printed" "$log.prn" 2>/dev/null; then
	status="the printer output $log.prn lacks '$printed'"
fi
secs=$((end - start))
if [ "$status" = ok ]; then
	echo "PASS $name ($target, card $card, ${secs}s)${stop:+ $stop}"
	exit 0
fi
echo "FAIL $name ($target, card $card, ${secs}s): $status${stop:+ ($stop)} (transcript $log, stderr $log.err)" >&2
echo "--- last lines of $log:" >&2
tail -n 25 "$log" >&2
if [ -s "$log.err" ]; then
	echo "--- last lines of $log.err:" >&2
	tail -n 10 "$log.err" >&2
fi
exit 1
