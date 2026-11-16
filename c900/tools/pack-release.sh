#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# Pack Exec900's release archives from a finished build:
# `sh tools/pack-release.sh TAG [OUT]' (OUT defaults to build/c900; `make
# release TAG=...' builds what is needed first).  Writes OUT/dist/:
#   c900-exec900-TAG          Boot/ (the boot volume), System/ (SYS:)
#   c900-exec900-headers-TAG  include/, fd/, lib/, platform/
#   c900-exec900-tools-TAG    tools/
# Each has VERSION, .provenance, .contents and licenses/; all three
# unpack into one tree.
set -e

c900=$(cd "$(dirname "$0")/.." && pwd)
tag=${1:?usage: pack-release.sh TAG [OUT]}
out=$(cd "${2:-$c900/../build/c900}" && pwd)
dist=$out/dist
tc=${C900_TOOLCHAIN:-$c900/deps/commodore-900-toolchain}
kb=${KBOOTSRC:-$c900/deps/commodore-900-kboot}

# A dependency's version: its release VERSION, else its checkout's tag.
depver() {
	if [ -f "$1/VERSION" ]; then echo "v$(cat "$1/VERSION")"
	else git -C "$1" describe --tags --always 2>/dev/null || echo unknown
	fi
}

commit=$(git -C "$c900" rev-parse HEAD)
dirty=0
[ -z "$(git -C "$c900" status --porcelain)" ] || dirty=1
built=$(date -u +%Y-%m-%dT%H:%M:%SZ)
toolchain=$(depver "$tc")
kboot=$(depver "$kb")
bootinfo=$(sed -n 's/^#define[ 	]*BI_VERSION[ 	]*\([0-9]*\).*/\1/p' "$kb/include/bootinfo.h")

# $1 package  $2... licence files
start() {
	pkg=$1
	d=$dist/c900-$pkg-$tag
	shift
	mkdir -p "$d/licenses"
	for f; do cp "$f" "$d/licenses/"; done
	echo "${tag#v}" > "$d/VERSION"
}

# .contents is md5sum's listing of every other file; contentid its sha1.
finish() {
	(cd "$d" && find . -type f ! -name .contents ! -name .provenance |
	  LC_ALL=C sort | sed 's|^\./||' | xargs md5sum) > "$d/.contents"
	cat > "$d/.provenance" <<-EOF
	kind=$pkg
	package=$pkg
	commit=$commit
	dirty=$dirty
	built=$built
	version=${tag#v}
	toolchain=$toolchain
	kboot=$kboot
	bootinfo=$bootinfo
	contentid=$(sha1sum "$d/.contents" | cut -c1-12)
	EOF
	tar czf "$dist/c900-$pkg-$tag.tar.gz" -C "$dist" "c900-$pkg-$tag"
	echo "$dist/c900-$pkg-$tag.tar.gz"
}

rm -rf "$dist"

# The boot volume (DH0:) is flat, names at most 14 bytes.
MODULES="dos.library con-handler v7fs-handler ram-handler ieeedoubbas
	ieeedoubtran lance.device"
start exec900 "$c900/../LICENSE" "$c900/../LICENSE-MIT" \
	"$c900/LICENSING.md" "$c900/libc/NOTICE"
mkdir -p "$d/Boot" "$d/System/C" "$d/System/L" "$d/System/S" \
	"$d/System/Libs" "$d/System/Devs" "$d/System/Fonts"
cp "$out/exec900.sys" "$d/Boot/"
for m in $MODULES lr.display hr.display lc.display gameport.dev; do
	cp "$out/hunks/$m" "$d/Boot/"
done
cp "$out/progs/dosboot" "$d/Boot/DOSBoot"
printf '%s\n' $MODULES > "$d/Boot/Modules"
echo DOSBoot > "$d/Boot/Startup"
cp "$out"/c/* "$d/System/C/"
cp "$out/hunks/Shell-Seg" "$d/System/L/"
cp "$c900/dos/Startup-Sequence" "$d/System/S/"
finish

start exec900-headers "$c900/../LICENSE-MIT" "$c900/LICENSING.md"
mkdir -p "$d/lib" "$d/platform"
cp -R "$c900/include" "$c900/fd" "$d/"
cp -R "$out/include/." "$d/include/"
cp "$c900/lib/startup.s" "$c900/lib/amiga.c" "$d/lib/"
cp "$c900/platform/progabs.s" "$c900/platform/portio.s" "$d/platform/"
finish

start exec900-tools "$c900/../LICENSE" "$c900/../LICENSE-MIT" \
	"$c900/LICENSING.md"
mkdir -p "$d/tools"
for t in genlvo.py mkhunk.py mkdisk.py symcheck.py runtest.sh \
	cohfs.py sparse.py echowire.py sendwire.py; do
	cp "$c900/tools/$t" "$d/tools/"
done
finish
