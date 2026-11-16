/* SPDX-License-Identifier: MIT */
/*
 * C:StackUse's listing: a CreateTask Task that went a known depth into
 * its 4,096-byte stack is reported with that use (plus at most the
 * interrupt headroom), every line's free bytes are its size less its use,
 * the root Task and the DOS handlers are listed, the command's own
 * process shows its command stack and its process stack, and the pool
 * line matches AvailMem.  (Not named stackuse: the shell would find this
 * root program in SYS: before C:StackUse.)
 */
#define TNAME	"taskstacks"
#include "dostest.h"

#define DEEPSTK	4096
#define LEVELS	12
#define SLACK	256		/* the interrupt headroom (c900.h IRQHEADROOM) */

static char hdr[] = "Task                 Lower  Size  Used  Free State\n";
static char buf[1600];
static struct Task *parent;
static UWORD depth;

/* Each level writes a 120-byte frame; the deepest records its depth. */
static int
dive(n)
int n;
{
	UBYTE f[120];
	register int i;

	for (i = 0; i < sizeof f; i++)
		f[i] = 0x55;
	if (n == 0) {
		depth = (UWORD)((UBYTE *)FindTask(NULL)->tc_SPUpper - f);
		return (f[0]);
	}
	return (dive(n - 1) + f[n]);
}

static void
deep()
{
	dive(LEVELS);
	Signal(parent, SIGBREAKF_CTRL_E);
	Wait(SIGBREAKF_CTRL_C);
}

/* A decimal or hex number after blanks at *pp; pp moves past it. */
static LONG
num(pp, base)
char **pp;
int base;
{
	register char *p;
	register LONG v;
	register int d;

	for (p = *pp; *p == ' '; p++)
		;
	for (v = 0;; p++) {
		if (*p >= '0' && *p <= '9')
			d = *p - '0';
		else if (base == 16 && *p >= 'a' && *p <= 'f')
			d = *p - 'a' + 10;
		else
			break;
		v = v * base + d;
	}
	*pp = p;
	return (v);
}

/* p starts with s. */
static int
prefix(p, s)
register char *p, *s;
{
	while (*s != '\0' && *p == *s)
		p++, s++;
	return (*s == '\0');
}

/* The line for name, NULL when none; with state, the line must end so. */
static char *
line(name, state)
char *name, *state;
{
	register char *p, *a, *b;

	for (p = buf; *p != '\0'; ) {
		for (a = p, b = name; *a == *b && *b != '\0'; a++, b++)
			;
		if (*b == '\0' && *a == ' ') {
			if (state == NULL)
				return (p);
			while (*a != '\n' && *a != '\0')
				a++;
			for (b = state; *b != '\0'; b++)
				;
			if (*a == '\n' && prefix(a + 1 - (b - state), state))
				return (p);
		}
		while (*p != '\n' && *p != '\0')
			p++;
		if (*p == '\n')
			p++;
	}
	return (NULL);
}

/* Check one Task line's numbers; its size and use come back. */
static int
fields(p, size, used)
char *p;
LONG *size, *used;
{
	LONG f;

	p += 21;
	num(&p, 16);
	*size = num(&p, 10);
	*used = num(&p, 10);
	f = num(&p, 10);
	return (*used <= *size && *used + f == *size && *p == ' ');
}

static int
dtest()
{
	struct TagItem t[2];
	struct Task *d;
	char *p;
	BPTR fh;
	LONG n, size, used, total;
	char nm[21];
	int i;

	parent = FindTask(NULL);
	SetSignal(0L, SIGBREAKF_CTRL_E);
	CHECK((d = CreateTask("deep", 0L, (TASKPROC)deep, (ULONG)DEEPSTK)) !=
	    NULL, "CreateTask");
	Wait(SIGBREAKF_CTRL_E);
	CHECK((fh = Open("CON:", MODE_NEWFILE)) != NULL, "Open CON:");
	t[0].ti_Tag = SYS_Output;
	t[0].ti_Data = (ULONG)fh;
	t[1].ti_Tag = TAG_DONE;
	n = SystemTagList("StackUse >RAM:su", t);
	Close(fh);
	Signal(d, SIGBREAKF_CTRL_C);
	CHECK(n == 0, "StackUse failed");
	CHECK((fh = Open("RAM:su", MODE_OLDFILE)) != NULL, "no RAM:su");
	n = Read(fh, buf, (LONG)sizeof(buf) - 1);
	Close(fh);
	buf[n < 0 ? 0 : n] = '\0';
	KPutStr(buf);
	CHECK(n > 0 && n < (LONG)sizeof(buf) - 1, "listing size");
	CHECK(prefix(buf, hdr), "header");
	for (p = buf; *p != '\n'; p++)
		;
	for (p++; *p != '\0' && !prefix(p, "Stack pool: "); ) {
		CHECK(fields(p, &size, &used), "a line's numbers");
		while (*p != '\n' && *p != '\0')
			p++;
		if (*p == '\n')
			p++;
	}
	CHECK((p = line("deep", " wait\n")) != NULL, "no deep Task line");
	CHECK(fields(p, &size, &used) && size == DEEPSTK &&
	    used >= depth && used <= depth + SLACK, "deep Task's use");
	xKPrintf("taskstacks: deep reached %u, listed %ld\n", depth, used);
	CHECK(line("root", " wait\n") != NULL, "no root line");
	CHECK(line("con-handler", NULL) != NULL &&
	    line("v7fs-handler", NULL) != NULL &&
	    line("ram-handler", NULL) != NULL, "no handler lines");
	for (p = buf; *p != '\0' && !prefix(p + 45, "run\n"); ) {
		while (*p != '\n' && *p != '\0')
			p++;
		if (*p == '\n')
			p++;
	}
	CHECK(*p != '\0' && fields(p, &size, &used) && size == 4000,
	    "the command's line (4,000-byte stack)");
	for (i = 0; i < 20 && p[i] != ' '; i++)
		nm[i] = p[i];
	nm[i] = '\0';
	CHECK(line(nm, " process\n") != NULL, "no process stack line");
	p = buf;
	while (*p != '\0' && !prefix(p, "Stack pool: "))
		p++;
	CHECK(*p != '\0', "no pool line");
	p += 12;
	n = num(&p, 10);
	CHECK(prefix(p, " of "), "pool line");
	p += 4;
	total = num(&p, 10);
	CHECK(total == AvailMem(MEMF_STACK | MEMF_TOTAL) && n < total,
	    "pool total");
	return (0);
}
