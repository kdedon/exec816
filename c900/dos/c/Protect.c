/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Protect FILE/A,FLAGS,ADD/S,SUB/S,ALL/S,QUIET/S: set the protection bits
 * hsparwed (h s p a set when named; r w e d allowed when named), or add
 * (+flags, ADD) or take away (-flags, SUB) the named ones.  Without
 * flags, each bit is shown.  ALL is accepted and ignored.
 */
#include "cargs.h"

static char bits[] = "hsparwed";

int
main(args, len)
char *args;
LONG len;
{
	static struct FileInfoBlock fib;
	char w[12], *file, *f;
	struct RDArgs *rd;
	LONG a[6];
	register int i, mode, rc;
	register LONG named, prot;
	BPTR lk;

	a[0] = a[1] = a[2] = a[3] = a[4] = a[5] = 0;
	if ((rd = rdargs("FILE/A,FLAGS,ADD/S,SUB/S,ALL/S,QUIET/S", a)) == NULL)
		return (RETURN_ERROR);
	file = (char *)a[0];
	named = 0;
	mode = a[2] ? '+' : a[3] ? '-' : 0;
	if ((f = (char *)a[1]) != NULL)
		for (; *f != '\0'; f++)
			if (*f == '+' || *f == '-')
				mode = *f;
			else
				for (i = 0; i < 8; i++)
					if ((*f | 0x20) == bits[i])
						named |= 1L << (7 - i);
	rc = RETURN_OK;
	if ((lk = Lock(file, SHARED_LOCK)) == NULL ||
	    Examine(lk, &fib) == DOSFALSE) {
		UnLock(lk);
		rc = fail(file);
		goto out;
	}
	UnLock(lk);
	prot = fib.fib_Protection ^ 0x0F;	/* set bits now allow */
	if (a[1] == 0 && mode == 0) {
		for (i = 0; i < 8; i++)
			w[i] = prot & 1L << (7 - i) ? bits[i] : '-';
		w[8] = '\0';
		Printf("%s %s\n", file, w);
	} else {
		prot = mode == '+' ? prot | named : mode == '-' ? prot &
		    ~named : (prot & ~0xFFL) | named;
		if (SetProtection(file, prot ^ 0x0F) == DOSFALSE)
			rc = fail(file);
	}
out:
	FreeArgs(rd);
	return (rc);
}
