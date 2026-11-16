/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Type FROM/A/M,TO/K,OPT/K,HEX/S,NUMBER/S: copy each file to the
 * output (or TO).  NUMBER (OPT N) numbers the lines, HEX (OPT H) dumps
 * the bytes in hexadecimal and text.  Ctrl-C stops it.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <lvo/dos_lib.h>

static char buf[256];

/* One file to out: 0, or the return code to stop with. */
static int
type(fh, out, how)
BPTR fh, out;
int how;
{
	register int i, n;
	register LONG k;
	LONG a[6];

	for (k = 0; ; k++) {
		if (CheckSignal(SIGBREAKF_CTRL_C)) {
			PrintFault(ERROR_BREAK, NULL);
			return (RETURN_WARN);
		}
		if (how == 'H') {
			if ((n = FRead(fh, buf, 1L, 16L)) <= 0)
				break;
			for (i = n; i < 16; i++)
				buf[i] = 0;
			a[0] = k << 4;
			for (i = 0; i < 4; i++)
				a[i + 1] = (LONG)(buf[i * 4] & 0xFF) << 24 |
				    (LONG)(buf[i * 4 + 1] & 0xFF) << 16 |
				    (LONG)(buf[i * 4 + 2] & 0xFF) << 8 |
				    buf[i * 4 + 3] & 0xFF;
			VFPrintf(out, "%04lx: %08lx %08lx %08lx %08lx ", (APTR)a);
			for (i = 0; i < n; i++)
				if (buf[i] < ' ' || buf[i] > '~')
					buf[i] = '.';
			buf[n] = '\n';
			FWrite(out, buf, 1L, (ULONG)n + 1);
		} else if (how == 'N') {
			if (FGets(fh, buf, (ULONG)sizeof(buf)) == NULL)
				break;
			a[0] = k + 1;
			VFPrintf(out, "%5ld ", (APTR)a);
			FPuts(out, buf);
		} else {
			if ((k = Read(fh, buf, (LONG)sizeof(buf))) <= 0)
				break;
			Write(out, buf, k);
		}
	}
	return (0);
}

int
main(args, len)
char *args;
LONG len;
{
	struct RDArgs *rd;
	LONG a[5];
	register char **name, *o;
	BPTR fh, out;
	int rc, how, r;

	a[0] = a[1] = a[2] = a[3] = a[4] = 0;
	if ((rd = ReadArgs("FROM/A/M,TO/K,OPT/K,HEX/S,NUMBER/S", a,
	    (struct RDArgs *)NULL)) == NULL) {
		PrintFault(IoErr(), NULL);
		return (RETURN_ERROR);
	}
	how = a[3] ? 'H' : a[4] ? 'N' : 0;
	if ((o = (char *)a[2]) != NULL)
		how = *o == 'h' || *o == 'H' ? 'H' : *o == 'n' || *o == 'N' ?
		    'N' : how;
	rc = RETURN_OK;
	out = Output();
	if (a[1] != 0 && (out = Open((char *)a[1], MODE_NEWFILE)) == NULL) {
		PrintFault(IoErr(), (char *)a[1]);
		FreeArgs(rd);
		return (RETURN_ERROR);
	}
	for (name = (char **)a[0]; *name != NULL && rc != RETURN_WARN;
	    name++) {
		if ((fh = Open(*name, MODE_OLDFILE)) == NULL) {
			PrintFault(IoErr(), *name);
			rc = RETURN_ERROR;
			continue;
		}
		if ((r = type(fh, out, how)) != 0)
			rc = r;
		Close(fh);
	}
	if (out != Output())
		Close(out);
	FreeArgs(rd);
	return (rc);
}
