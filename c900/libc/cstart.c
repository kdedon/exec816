/* SPDX-License-Identifier: MIT */
/*
 * C program startup on AmigaDOS (libc/crt0.s enters here).  dos.library
 * calls the program as LONG start(char *args, LONG len); cstart opens
 * dos.library, sets up the standard descriptors, splits the arguments
 * into argv as AmigaDOS's C startup does, and calls main(argc, argv,
 * envp).  exit and _exit come back here through longjmp, so the
 * program's resources are released in one place and its status is the
 * command's result.
 */
#include <setjmp.h>
#include "libcint.h"

#define NAMELEN	64		/* argv[0], the command name */

struct DosLibrary *DOSBase;
static char *envnul = NULL;
char **environ = &envnul;
APTR _mpool;
BPTR _cwdlk;
BPTR _cwdorig;
void (*_exithk)();

static jmp_buf xjmp;
static int xrc;

char *malloc();
void _finish();
void exit();

/* Split the argument line s (n bytes) into NUL-terminated words at d and
 * return their count.  Blanks separate words; a word starting with "
 * runs to the next ", inside which *" is ", ** is *, *N a newline and
 * *E an escape.  A newline or NUL ends the line. */
static int
splitargs(s, n, d)
register char *s;
int n;
register char *d;
{
	register char *e;
	register int c;
	int argc;

	e = s + n;
	argc = 0;
	for (;;) {
		while (s < e && (*s == ' ' || *s == '\t'))
			s++;
		if (s >= e || *s == '\n' || *s == '\0')
			break;
		argc++;
		if (*s == '"') {
			for (s++; s < e && *s != '"' && *s != '\n' && *s != '\0';
			    s++) {
				if ((c = *s) == '*' && s + 1 < e) {
					c = *++s;
					if (c == 'N' || c == 'n')
						c = '\n';
					else if (c == 'E' || c == 'e')
						c = 0x1B;
				}
				*d++ = c;
			}
			if (s < e && *s == '"')
				s++;
		} else {
			while (s < e && *s != ' ' && *s != '\t' && *s != '\n' &&
			    *s != '\0')
				*d++ = *s++;
		}
		*d++ = '\0';
	}
	return (argc);
}

static void
cmain(args, n)
char *args;
int n;
{
	register char *p, **av;
	register int i;
	char *buf, **argv;
	int argc;

	if ((buf = malloc(n + 1 + NAMELEN)) == NULL)
		_exit(RETURN_FAIL);
	if (GetProgramName(buf, (LONG)NAMELEN) == DOSFALSE)
		buf[0] = '\0';
	p = buf + NAMELEN;
	argc = splitargs(args, n, p) + 1;
	if ((argv = (char **)malloc((argc + 1) * sizeof(char *))) == NULL)
		_exit(RETURN_FAIL);
	av = argv;
	*av++ = buf;
	for (i = 1; i < argc; i++) {
		*av++ = p;
		while (*p++ != '\0')
			;
	}
	*av = NULL;
	exit(main(argc, argv, environ));
}

LONG
cstart(args, len)
char *args;
LONG len;
{
	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 36L)) ==
	    NULL)
		return ((LONG)RETURN_FAIL);
	_cwdorig = ((struct Process *)FindTask(NULL))->pr_CurrentDir;
	_fdinit();
	if (setjmp(xjmp) == 0)
		cmain(args, (int)len);
	_fdexit();
	if (_cwdlk != NULL) {
		CurrentDir(_cwdorig);
		UnLock(_cwdlk);
	}
	if (_mpool != NULL)
		DeletePool(_mpool);
	CloseLibrary((struct Library *)DOSBase);
	return ((LONG)xrc);
}

void
exit(s)
int s;
{
	if (_exithk != NULL)
		(*_exithk)();
	_finish();
	_exit(s);
}

void
_exit(s)
int s;
{
	xrc = s;
	longjmp(xjmp, 1);
}
