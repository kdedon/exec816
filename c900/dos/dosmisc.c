/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * dos.library: per-process state (IoErr, Input/Output, the CLI's
 * strings), error text, dates, DOS objects and path strings.  As on the Amiga, these calls belong to processes; called
 * from a plain Task, IoErr state is dropped and the streams are NULL.
 */
#include "dosint.h"
#include <devices/timer.h>

struct Device *TimerBase;	/* set by dlinit */

/* The calling process, or NULL for a plain Task. */
struct Process *
dme()
{
	register struct Process *p;

	p = (struct Process *)FindTask(NULL);
	return (p->pr_Task.tc_Node.ln_Type == NT_PROCESS ? p : NULL);
}

LONG
dstrlen(s)
register char *s;
{
	register char *p;

	for (p = s; *p != '\0'; p++)
		;
	return ((LONG)(p - s));
}

static int
dlower(c)
register int c;
{
	return (c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c);
}

/* Compare a with b, ignoring ASCII case, up to n bytes, NUL or (in a) a
 * colon: 0 when equal. */
int
dnamecmp(a, b, n)
register char *a, *b;
register int n;
{
	for (; n > 0; n--, a++, b++) {
		if (*a == ':')
			return (*b != '\0');
		if (dlower(*a) != dlower(*b))
			return (1);
		if (*a == '\0')
			return (0);
	}
	return (0);
}

/* The first colon in s, or NULL. */
char *
dcolon(s)
register char *s;
{
	for (; *s != '\0'; s++)
		if (*s == ':')
			return (s);
	return (NULL);
}

struct DosInfo *
ddi()
{
	return ((struct DosInfo *)DOSBase->dl_Root->rn_Info);
}

/* Set IoErr; return the old value. */
LONG
dseterr(e)
LONG e;
{
	register struct Process *p;
	LONG o;

	if ((p = dme()) == NULL)
		return (0);
	o = p->pr_Result2;
	p->pr_Result2 = e;
	return (o);
}

/* Set IoErr to e and return DOSFALSE. */
LONG
dfail(e)
LONG e;
{
	dseterr(e);
	return (DOSFALSE);
}

LONG
xIoErr(db)
DB db;
{
	register struct Process *p;

	return ((p = dme()) != NULL ? p->pr_Result2 : 0L);
}

LONG
xSetIoErr(db, e)
DB db;
LONG e;
{
	return (dseterr(e));
}

BPTR
xInput(db)
DB db;
{
	register struct Process *p;

	return ((p = dme()) != NULL ? p->pr_CIS : NULL);
}

BPTR
xOutput(db)
DB db;
{
	register struct Process *p;

	return ((p = dme()) != NULL ? p->pr_COS : NULL);
}

BPTR
xSelectInput(db, fh)
DB db;
BPTR fh;
{
	register struct Process *p;
	BPTR o;

	p = dme();
	o = p->pr_CIS;
	p->pr_CIS = fh;
	return (o);
}

BPTR
xSelectOutput(db, fh)
DB db;
BPTR fh;
{
	register struct Process *p;
	BPTR o;

	p = dme();
	o = p->pr_COS;
	p->pr_COS = fh;
	return (o);
}

struct CommandLineInterface *
xCli(db)
DB db;
{
	register struct Process *p;

	return ((p = dme()) != NULL ?
	    (struct CommandLineInterface *)p->pr_CLI : NULL);
}

char *
xGetArgStr(db)
DB db;
{
	return ((char *)dme()->pr_Arguments);
}

char *
xSetArgStr(db, s)
DB db;
char *s;
{
	register struct Process *p;
	char *o;

	p = dme();
	o = (char *)p->pr_Arguments;
	p->pr_Arguments = (UBYTE *)s;
	return (o);
}

struct MsgPort *
xGetConTask(db)
DB db;
{
	return ((struct MsgPort *)dme()->pr_ConsoleTask);
}

struct MsgPort *
xSetConTask(db, port)
DB db;
struct MsgPort *port;
{
	register struct Process *p;
	struct MsgPort *o;

	p = dme();
	o = (struct MsgPort *)p->pr_ConsoleTask;
	p->pr_ConsoleTask = (APTR)port;
	return (o);
}

struct MsgPort *
xGetFSTask(db)
DB db;
{
	return ((struct MsgPort *)dme()->pr_FileSystemTask);
}

struct MsgPort *
xSetFSTask(db, port)
DB db;
struct MsgPort *port;
{
	register struct Process *p;
	struct MsgPort *o;

	p = dme();
	o = (struct MsgPort *)p->pr_FileSystemTask;
	p->pr_FileSystemTask = (APTR)port;
	return (o);
}

/* Copy s into the CLI string d of size n; DOSFALSE without a CLI or room. */
static LONG
dsetstr(d, n, s)
register char *d;
LONG n;
register char *s;
{
	if (d == NULL)
		return (DOSFALSE);
	if (dstrlen(s) >= n)
		return (dfail(ERROR_LINE_TOO_LONG));
	while ((*d++ = *s++) != '\0')
		;
	return (DOSTRUE);
}

/* Copy CLI string s into buf of size len, cut to fit; DOSFALSE (and an
 * empty buf) without a CLI. */
static LONG
dgetstr(s, buf, len)
register char *s, *buf;
LONG len;
{
	if (len <= 0)
		return (DOSFALSE);
	if (s == NULL) {
		*buf = '\0';
		return (DOSFALSE);
	}
	for (; --len > 0 && *s != '\0'; )
		*buf++ = *s++;
	*buf = '\0';
	return (DOSTRUE);
}

#define CLIP	((struct CommandLineInterface *)dme()->pr_CLI)
#define CLIS(f)	(CLIP != NULL ? CLIP->f : (char *)NULL)

LONG
xSetCurDirName(db, s)
DB db;
char *s;
{
	return (dsetstr(CLIS(cli_SetName), (LONG)CLI_SETNAME, s));
}

LONG
xGetCurDirName(db, buf, len)
DB db;
char *buf;
LONG len;
{
	return (dgetstr(CLIS(cli_SetName), buf, len));
}

LONG
xSetProgName(db, s)
DB db;
char *s;
{
	return (dsetstr(CLIS(cli_CommandName), (LONG)CLI_COMMANDNAME, s));
}

LONG
xGetProgName(db, buf, len)
DB db;
char *buf;
LONG len;
{
	return (dgetstr(CLIS(cli_CommandName), buf, len));
}

LONG
xSetPrompt(db, s)
DB db;
char *s;
{
	return (dsetstr(CLIS(cli_Prompt), (LONG)CLI_PROMPT, s));
}

LONG
xGetPrompt(db, buf, len)
DB db;
char *buf;
LONG len;
{
	return (dgetstr(CLIS(cli_Prompt), buf, len));
}

ULONG
xCheckSignal(db, mask)
DB db;
ULONG mask;
{
	return (SetSignal(0L, mask) & mask);
}

static struct dmsg {
	WORD m_Code;
	char *m_Text;
} dmsgs[] = {
	ERROR_NO_FREE_STORE, "not enough memory available",
	ERROR_BAD_TEMPLATE, "bad template",
	ERROR_BAD_NUMBER, "bad number",
	ERROR_REQUIRED_ARG_MISSING, "required argument missing",
	ERROR_KEY_NEEDS_ARG, "value after keyword missing",
	ERROR_TOO_MANY_ARGS, "wrong number of arguments",
	ERROR_UNMATCHED_QUOTES, "unmatched quotes",
	ERROR_LINE_TOO_LONG, "line too long",
	ERROR_OBJECT_IN_USE, "object is in use",
	ERROR_OBJECT_EXISTS, "object already exists",
	ERROR_DIR_NOT_FOUND, "directory not found",
	ERROR_OBJECT_NOT_FOUND, "object not found",
	ERROR_ACTION_NOT_KNOWN, "packet request type unknown",
	ERROR_INVALID_COMPONENT_NAME, "invalid filename component",
	ERROR_OBJECT_WRONG_TYPE, "object is not of required type",
	ERROR_DISK_WRITE_PROTECTED, "disk is write protected",
	ERROR_DEVICE_NOT_MOUNTED, "device (or volume) is not mounted",
	ERROR_SEEK_ERROR, "seek failure",
	ERROR_DISK_FULL, "disk is full",
	ERROR_RENAME_ACROSS_DEVICES, "rename across devices attempted",
	ERROR_DIRECTORY_NOT_EMPTY, "directory not empty",
	ERROR_COMMENT_TOO_BIG, "comment is too big",
	ERROR_DELETE_PROTECTED, "file is protected from deletion",
	ERROR_WRITE_PROTECTED, "file is write protected",
	ERROR_READ_PROTECTED, "file is read protected",
	ERROR_NO_MORE_ENTRIES, "no more entries in directory",
	ERROR_BAD_HUNK, "file is not executable",
	ERROR_NOT_IMPLEMENTED, "function not implemented",
	ERROR_BUFFER_OVERFLOW, "buffer overflow",
	ERROR_BREAK, "***Break",
	ERROR_NOT_EXECUTABLE, "file is not executable",
	0, NULL
};

/* RawDoFmt into a string: pdata is the next byte. */
void
dnumput(c, p)
int c;
char **p;
{
	*(*p)++ = c;
}

/* "header: text" in buffer, cut to len; the length, 0 for code 0. */
LONG
xFault(db, code, header, buf, len)
DB db;
LONG code;
char *header, *buf;
LONG len;
{
	register struct dmsg *m;
	register char *p, *s;
	char num[16], *np;
	LONG n[1];

	if (code == 0 || len <= 0) {
		if (len > 0)
			*buf = '\0';
		return (0);
	}
	for (m = dmsgs; m->m_Code != 0 && m->m_Code != code; m++)
		;
	s = m->m_Text;
	if (s == NULL) {
		n[0] = code;
		np = num;
		RawDoFmt("error %ld", (APTR)n, (APTR)dnumput, (APTR)&np);
		s = num;
	}
	p = buf;
	if (header != NULL) {
		while (*header != '\0' && --len > 0)
			*p++ = *header++;
		for (header = ": "; *header != '\0' && --len > 0; )
			*p++ = *header++;
	}
	while (*s != '\0' && --len > 0)
		*p++ = *s++;
	*p = '\0';
	return ((LONG)(p - buf));
}

LONG
xPrintFault(db, code, header)
DB db;
LONG code;
char *header;
{
	char buf[80];
	register LONG n;

	n = xFault(db, code, header, buf, (LONG)sizeof(buf) - 1);
	if (n == 0)
		return (DOSFALSE);
	buf[n] = '\n';
	xWrite(db, xOutput(db), buf, n + 1);
	dseterr(code);
	return (DOSTRUE);
}

/* The system time (timer.device, seconds since 1978-01-01). */
struct DateStamp *
xDateStamp(db, ds)
DB db;
register struct DateStamp *ds;
{
	struct timeval tv;
	ULONG t;

	tv.tv_secs = tv.tv_micro = 0;
	if (TimerBase != NULL)
		GetSysTime(&tv);
	t = tv.tv_secs;
	ds->ds_Days = t / 86400L;
	t %= 86400L;
	ds->ds_Minute = t / 60;
	ds->ds_Tick = (t % 60) * TICKS_PER_SECOND +
	    tv.tv_micro / (1000000L / TICKS_PER_SECOND);
	return (ds);
}

/* timeout in Amiga ticks (1/50 s); the system tick is 100 Hz. */
void
xDelay(db, timeout)
DB db;
LONG timeout;
{
	if (timeout > 0)
		Sleep((ULONG)timeout * 2);
}

/* Decimal, with an optional sign: the characters used, or -1. */
LONG
xStrToLong(db, s, val)
DB db;
char *s;
LONG *val;
{
	register char *p;
	register LONG v;
	int neg;

	for (p = s; *p == ' ' || *p == '\t'; p++)
		;
	neg = 0;
	if (*p == '-' || *p == '+')
		neg = *p++ == '-';
	if (*p < '0' || *p > '9')
		return (-1);
	for (v = 0; *p >= '0' && *p <= '9'; p++)
		v = v * 10 + (*p - '0');
	*val = neg ? -v : v;
	return ((LONG)(p - s));
}

char *
xFilePart(db, path)
DB db;
char *path;
{
	register char *p, *f;

	for (f = p = path; *p != '\0'; p++)
		if (*p == ':' || *p == '/')
			f = p + 1;
	return (f);
}

char *
xPathPart(db, path)
DB db;
char *path;
{
	register char *p, *f;

	for (f = p = path; *p != '\0'; p++)
		if (*p == ':')
			f = p + 1;
		else if (*p == '/')
			f = p;
	return (f);
}

/* Append file to dir with a separator where needed; DOSFALSE without
 * room in size. */
LONG
xAddPart(db, dir, file, size)
DB db;
char *dir, *file;
ULONG size;
{
	register char *d;
	register LONG n;

	n = dstrlen(dir);
	d = dir + (UWORD)n;
	if (*file == ':') {
		d = dir;
		n = 0;
	} else if (n != 0 && d[-1] != ':' && d[-1] != '/')
		n++;
	if (n + dstrlen(file) >= size)
		return (dfail(ERROR_LINE_TOO_LONG));
	if (d != dir + (UWORD)n)
		*d++ = '/';
	while ((*d++ = *file++) != '\0')
		;
	return (DOSTRUE);
}

/* DOS_FILEHANDLE, DOS_FIB, DOS_STDPKT, DOS_CLI (with its strings) and
 * DOS_RDARGS; tags are ignored. */
static UWORD dobjsize[] = {
	sizeof(struct DFH), 0, sizeof(struct FileInfoBlock),
	sizeof(struct StandardPacket),
	sizeof(struct CommandLineInterface) + CLI_SETNAME + CLI_COMMANDNAME +
	    CLI_PROMPT + CLI_COMMANDFILE, sizeof(struct RDArgs)
};

APTR
xAllocDosObj(db, type, tags)
DB db;
ULONG type;
APTR tags;
{
	register struct CommandLineInterface *c;
	register struct StandardPacket *sp;
	register char *s;
	APTR p;

	if (type > DOS_RDARGS || type == DOS_EXALLCONTROL) {
		dseterr(ERROR_BAD_NUMBER);
		return (NULL);
	}
	if ((p = AllocVec((ULONG)dobjsize[(int)type],
	    MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
		dseterr(ERROR_NO_FREE_STORE);
		return (NULL);
	}
	switch ((int)type) {
	case DOS_FILEHANDLE:
		((struct FileHandle *)p)->fh_Pos = -1;
		((struct FileHandle *)p)->fh_End = -1;
		break;
	case DOS_STDPKT:
		sp = (struct StandardPacket *)p;
		sp->sp_Msg.mn_Node.ln_Name = (char *)&sp->sp_Pkt;
		sp->sp_Pkt.dp_Link = &sp->sp_Msg;
		break;
	case DOS_CLI:
		c = (struct CommandLineInterface *)p;
		s = (char *)(c + 1);
		c->cli_SetName = s;
		c->cli_CommandName = s += CLI_SETNAME;
		c->cli_Prompt = s += CLI_COMMANDNAME;
		c->cli_CommandFile = s + CLI_PROMPT;
		c->cli_FailLevel = RETURN_ERROR;
		c->cli_DefaultStack = CLISTACK;
		dsetstr(c->cli_Prompt, (LONG)CLI_PROMPT, "%N> ");
		break;
	}
	return (p);
}

void
xFreeDosObj(db, type, p)
DB db;
ULONG type;
APTR p;
{
	FreeVec(p);
}
