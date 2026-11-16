/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Version [NAME] [VERSION n] [REVISION n] [FILE]: the Kickstart
 * (exec.library) version; or a library's, a device's, or a file's
 * $VER: string.  With VERSION (and REVISION) the result is RETURN_WARN
 * when what is found is older.
 */
#include "cargs.h"
#include <exec/io.h>

static char vbuf[80], rbuf[512];

/* The $VER: string of file name into vbuf, its version and revision. */
static int
fver(name, v, r)
char *name;
LONG *v, *r;
{
	static char tag[] = "$VER: ";
	register int c, m, n, i;
	register char *p;
	LONG k;
	BPTR fh;

	if ((fh = Open(name, MODE_OLDFILE)) == NULL)
		return (0);
	m = n = 0;
	for (k = i = 0; ; ) {
		if (i == k && (i = 0, k = Read(fh, rbuf, (LONG)sizeof(rbuf))) <= 0)
			break;
		c = rbuf[i++];
		if (m == 6) {
			if (c == '\0' || c == '\n' || c == '\r' ||
			    n == sizeof(vbuf) - 1)
				break;
			vbuf[n++] = c;
		} else if (c == tag[m])
			m++;
		else
			m = c == '$';
	}
	Close(fh);
	vbuf[n] = '\0';
	if (m != 6)
		return (0);
	for (p = vbuf; *p != '\0' && *p != ' '; p++)
		;
	if (StrToLong(p, v) > 0) {
		while (*++p == ' ' || (*p >= '0' && *p <= '9'))
			;
		if (*p == '.')
			StrToLong(p + 1, r);
	}
	return (1);
}

int
main(args, len)
char *args;
LONG len;
{
	static char name[WORDMAX];
	struct IOStdReq io;
	struct RDArgs *rd;
	register struct Library *lib;
	LONG v, r, wv, wr, a[6];
	register int file;

	a[0] = a[1] = a[2] = a[3] = a[4] = a[5] = 0;
	if ((rd = rdargs("NAME,VERSION/N,REVISION/N,FILE/S,FULL/S,RES/S", a))
	    == NULL)
		return (RETURN_ERROR);
	name[0] = '\0';
	if (a[0] != 0)
		scopy(name, (char *)a[0], WORDMAX);
	wv = a[1] != 0 ? *(LONG *)a[1] : -1;
	wr = a[2] != 0 ? *(LONG *)a[2] : -1;
	file = a[3] != 0;
	FreeArgs(rd);
	v = r = 0;
	lib = NULL;
	if (name[0] == '\0') {
		v = SysBase->LibNode.lib_Version;
		r = SysBase->LibNode.lib_Revision;
		Printf("Kickstart %ld.%ld\n", v, r);
	} else if (!file && (lib = OpenLibrary(name, 0L)) != NULL) {
		v = lib->lib_Version;
		r = lib->lib_Revision;
		CloseLibrary(lib);
	} else if (!file && OpenDevice(name, 0L, (struct IORequest *)&io,
	    0L) == 0) {
		lib = &io.io_Device->dd_Library;
		v = lib->lib_Version;
		r = lib->lib_Revision;
		CloseDevice((struct IORequest *)&io);
	} else if (fver(name, &v, &r))
		Printf("%s\n", vbuf);
	else {
		Printf("Could not find version information for %s\n", name);
		return (RETURN_ERROR);
	}
	if (lib != NULL)
		Printf("%s %ld.%ld\n", name, v, r);
	if (wv >= 0 && (v < wv || (v == wv && wr >= 0 && r < wr)))
		return (RETURN_WARN);
	return (RETURN_OK);
}
