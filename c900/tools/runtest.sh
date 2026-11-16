#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# runtest.sh EMUDIR BUDGET NAME IMAGE LOG [SOURCE]
#
# Boot IMAGE in the C900 emulator until the guest writes the stop doorbell
# (emustop) or goes idle, keep the console transcript in LOG with CRs
# stripped, and pass iff it holds a line "PASS NAME" and no FAIL line.
#
# SOURCE, the test's C file, may carry one-line directives:
#   /* EMUFLAGS: --ram=08-35 */   extra emulator options
#   /* EXPECT: text */            pass on a line containing text instead
#                                 of "PASS NAME" (a test that ends in a fault)
#   /* FLOPPY: pattern */         attach a fresh pattern floppy (mkdisk.py
#                                 --floppy) as LOG.fd
#   /* WIRE: 1 3 */               attach serial ports 1 and 3 to loopback
#                                 peers (echowire.py) on sockets in a
#                                 new directory under TMPDIR or /tmp: a
#                                 socket path holds about 100 bytes
#   /* SEND: 1 HEX */              attach serial port 1 to a peer
#                                 (sendwire.py) that sends the bytes
#                                 HEX spells once the guest sends one;
#                                 each further '/'-separated stage
#                                 after one more
#   /* KEYS: TICK:CODE,... */     keyboard bytes for the sim; without
#                                 EMUKEYS the test is skipped
#   /* EMUKEYS: T:HH,... */       keyboard bytes for the emulator (--key):
#                                 code HH, bit 7 the release, T loop ticks
#                                 after HWMARK is printed
#   /* HWMARK: text */            console text that starts the EMUKEYS
#                                 clock (--hw-mark)
#   /* BOOTS: N */                boot IMAGE N times in a row; the
#                                 transcripts are joined in LOG, and a
#                                 boot that stops early is a reset
#   /* FSCK: BASE... */           after each boot, check the V7
#                                 filesystem at each block BASE of
#                                 IMAGE (cohfs.py --check): damage fails
#   /* LANCE: loop|peer */       the LANCE card (and, for peer, a host on
#                                 its cable) for the sim; the emulator has
#                                 no LANCE, so the test is skipped
#   /* LC: card */                fit the LC card (--lc), which kboot's
#                                 probe then makes the console
#   /* MOUSE: TICK:DX:DY[:LRM],... */  fit the HR card (--hr) and move its
#                                 mouse (--mouse): TICK in model ticks
#                                 from the start (or HWMARK), 16 to
#                                 the emulator's loop tick; each event
#                                 sets all three buttons
#   EMUFLAGS words are split on blanks: --input text must have none.

if [ $# -ne 5 ] && [ $# -ne 6 ]; then
	echo "usage: runtest.sh EMUDIR BUDGET NAME IMAGE LOG [SOURCE]" >&2
	exit 2
fi
emu=$1 budget=$2 name=$3 img=$4 log=$5 src=${6:-}

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
emukeys=$(directive EMUKEYS)
if [ -n "$(directive KEYS)" ] && [ -z "$emukeys" ]; then
	echo "SKIP $name: no keys scripted for the emulator"
	exit 0
fi
if [ -n "$(directive LANCE)" ]; then
	echo "SKIP $name: the emulator has no LANCE card"
	exit 0
fi
emuflags=$(directive EMUFLAGS | ctlbytes)
[ "$(directive LC)" != card ] || emuflags="$emuflags --lc"
for k in $(echo "$emukeys" | tr , ' '); do
	emuflags="$emuflags --key=$k"
done
# A model tick is 1/24 us; the emulator's 100 Hz tick is 15,000 loop
# ticks, so one loop tick is 16 model ticks of guest time.
mouse=$(directive MOUSE)
[ -z "$mouse" ] || emuflags="$emuflags --hr"
for e in $(echo "$mouse" | tr , ' '); do
	emuflags="$emuflags --mouse=$(echo "$e" | awk -F: '{
		b = ($4 ~ /L/) * 4 + ($4 ~ /M/) * 2 + ($4 ~ /R/)
		printf "%d:%d:%d:%d", int($1 / 16), $2, $3, b }')"
done
hwmark=$(directive HWMARK)
expect=$(directive EXPECT)
if [ "$(directive FLOPPY)" = pattern ]; then
	python3 "$(dirname "$0")/mkdisk.py" --floppy "$log.fd" || exit 1
	emuflags="$emuflags --floppy=$log.fd"
fi
peers= wires=
for p in $(directive WIRE); do
	if [ -z "$wires" ]; then
		wires=$(mktemp -d "${TMPDIR:-/tmp}/c900w.XXXXXX") || exit 1
	fi
	python3 "$(dirname "$0")/echowire.py" "$wires/$p" &
	peers="$peers $!"
	emuflags="$emuflags --wire$p=$wires/$p"
done

set -- $(directive SEND)
if [ $# -eq 2 ]; then
	if [ -z "$wires" ]; then
		wires=$(mktemp -d "${TMPDIR:-/tmp}/c900w.XXXXXX") || exit 1
	fi
	python3 "$(dirname "$0")/sendwire.py" "$wires/$1" "$2" &
	peers="$peers $!"
	emuflags="$emuflags --wire$1=$wires/$1"
fi
boots=$(directive BOOTS)
fsck=$(directive FSCK)
# Boots that write the image run on a copy, so each run starts afresh.
if [ -n "$boots" ]; then
	cp "$img" "$log.img" || exit 1
	img=$log.img
fi
: >"$log.raw"
: >"$log.err"
rc=0 n=0
while [ $rc -eq 0 ] && [ $n -lt "${boots:-1}" ]; do
	n=$((n + 1))
	# emuflags is split into words on purpose.
	"$emu/bin/c900" --firmware="$emu/rom" --disk="$img" \
		--stop-on=port,idle --max="$budget" --require-stop $emuflags \
		${hwmark:+"--hw-mark=$hwmark"} \
		</dev/null >>"$log.raw" 2>>"$log.err"
	rc=$?
	for base in $fsck; do
		[ $rc -eq 0 ] || break
		python3 "$(dirname "$0")/cohfs.py" --check "$img" "$base" \
		    >>"$log.raw" || fsckbad=1
	done
done
for p in $peers; do
	kill "$p" 2>/dev/null
	wait "$p" 2>/dev/null
done
if [ -n "$wires" ]; then
	rm -f "$wires"/*
	rmdir "$wires"
fi
tr -d '\r' <"$log.raw" >"$log"
rm -f "$log.raw"
status=ok
if [ $rc -eq 3 ]; then
	status="the run exhausted --max=$budget without stopping"
elif [ $rc -ne 0 ]; then
	status="emulator exit status $rc"
elif [ -n "${fsckbad:-}" ]; then
	status="damaged filesystem"
elif grep -q '^FAIL' "$log"; then
	status="FAIL line"
elif [ -n "$expect" ]; then
	grep -qF -- "$expect" "$log" || status="no '$expect' line"
elif ! grep -q "^PASS $name\$" "$log"; then
	status="no 'PASS $name' line"
fi
if [ "$status" = ok ]; then
	echo "PASS $name"
	exit 0
fi
echo "FAIL $name: $status (transcript $log, emulator stderr $log.err)" >&2
echo "--- last lines of $log:" >&2
tail -n 25 "$log" >&2
if [ -s "$log.err" ]; then
	echo "--- last lines of $log.err:" >&2
	tail -n 10 "$log.err" >&2
fi
exit 1
