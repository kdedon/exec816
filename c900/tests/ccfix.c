/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Code-generation patterns that the unpatched MWC compiler rejects or
 * miscompiles (docs/internals.md, Toolchain facts).  make check runs this
 * test only when cc1 accepts the computed indirect call (Makefile CCFIXED).
 */
#include <exec/exec.h>

struct ccnode {
	WORD	cn_first;	/* at offset 0 */
	WORD	cn_second;
};

struct cchead {
	WORD		ch_pad;
	struct ccnode	*ch_a[4];
};

static struct ccnode n0, n1;
static struct cchead hd;

static LONG
target(x)
LONG x;
{
	return (x + 40);
}

/* The call address computed inline from a local, as LIBVEC does. */
static LONG
calllocal(x)
LONG x;
{
	char *lib;

	lib = (char *)target + 6;
	return ((*(LONG (*)())(lib - 6))(x));
}

static LONG
callreg(x)
LONG x;
{
	register char *lib;

	lib = (char *)target - 4;
	return ((*(LONG (*)())(lib + 4))(x));
}

/* A compare against an element's offset-0 member. */
static int
ismember(h, m, i)
struct cchead *h;
WORD *m;
int i;
{
	if (m == &h->ch_a[i]->cn_first)
		return (1);
	return (0);
}

int
main()
{
	int bad;

	bad = 0;
	if (calllocal(2L) != 42L) {
		KPutStr("FAIL ccfix: call through a local\n");
		bad = 1;
	}
	if (callreg(3L) != 43L) {
		KPutStr("FAIL ccfix: call through a register\n");
		bad = 1;
	}
	hd.ch_a[0] = &n0;
	hd.ch_a[1] = &n1;
	if (!ismember(&hd, &n0.cn_first, 0) || ismember(&hd, &n1.cn_first, 0)
	 || !ismember(&hd, &n1.cn_first, 1) || ismember(&hd, &n0.cn_first, 1)) {
		KPutStr("FAIL ccfix: compare with &a[i]->first\n");
		bad = 1;
	}
	if (!bad)
		KPutStr("PASS ccfix\n");
	return (0);
}
