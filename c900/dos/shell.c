/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The shell (L:Shell-Seg), a DOS program dos.library runs as a CLI
 * process for Execute and SystemTagList.  It runs its argument line
 * first, then reads command lines from the current input until the end
 * of input or EndCLI.  Execute pushes a script on the input; at its end
 * input returns to the script or standard input below it, as nested
 * AmigaDOS scripts do.  A SystemTagList shell (cli_CurrentInput NULL at
 * the start) ends when its scripts do.  It prompts (cli_Prompt: %N the
 * CLI number, %S the current directory) only when reading an
 * interactive standard input.  Every CLI process runs this one loaded
 * seglist, so a shell's state is an allocation of its own.
 *
 * Built-ins are AmigaOS 3.x's internal commands: Alias, Ask, CD, Echo,
 * Else, EndCLI, EndIf, EndShell, EndSkip, Execute, Failat, Fault, Get,
 * Getenv, If, Lab, NewCLI, NewShell, Path, Prompt, Quit, Resident, Run,
 * Set, Setenv, Skip, Stack, Unalias, Unset, Unsetenv and Why; CD, Echo,
 * Prompt and the variable ones parse their arguments with ReadArgs.  A
 * line's $name and ${name} become the variable's value (GetVar: local,
 * then global; *$ is a $), then an alias in the command's place becomes
 * its value, with [] (or the end) taking the rest of the line.  A
 * command is looked up in the
 * resident list (AddSegment), then the built-ins, then loaded from the
 * current directory, each cli_CommandDir directory (Path) and C:; a
 * name with a device or path part is loaded as given.  It runs in this
 * process through RunCommand on a stack of cli_DefaultStack longwords,
 * with the rest of the line (and a newline) as its arguments.  ">name"
 * and "<name" redirect a command's (or built-in's) output and input.
 * Its result becomes cli_ReturnCode and IoErr cli_Result2; in a script,
 * a result at or above cli_FailLevel, or Ctrl-D, ends every script.
 * Lines starting with ';' are comments.
 *
 * A script's dot lines: .key (or .k) names its arguments, with /A, /K
 * and /S; <name> and <name$default> on later lines are replaced by them
 * (.bra, .ket and .dollar change the three characters, .default sets a
 * default), <$$> by the CLI number.  ". text" is a comment.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/rdargs.h>
#include <dos/var.h>
#include <lvo/dos_lib.h>

LONG Printf();

#define LINEMAX	256
#define WORDMAX	80
#define NKEYS	10
#define POOL	256
#define MINSTK	1600L		/* Stack's least, bytes, as AmigaOS's */

/* Skipping in a script: none, to Else/EndIf, to EndIf, to Lab/EndSkip. */
#define SK_NONE	0
#define SK_IF	1
#define SK_ELSE	2
#define SK_LAB	3

/* An input: the standard input (base, only the head) or a script
 * Execute pushed. */
#define INHEAD \
	struct inp *i_Up; \
	BPTR i_File; \
	int i_Pos, i_End; \
	int i_Skip, i_Depth; \
	char i_Buf[LINEMAX]
struct inhead {
	INHEAD;
};
struct inp {
	INHEAD;
	int i_Keyed;
	char i_Bra, i_Ket, i_Dol;
	int i_NKeys, i_Fill;
	char *i_Name[NKEYS], *i_Val[NKEYS], *i_Def[NKEYS];
	UBYTE i_Flag[NKEYS];
	char i_Label[WORDMAX];
	char i_Args[LINEMAX];
	char i_Pool[POOL];
};
#define KF_A	1
#define KF_K	2
#define KF_S	4

/* A shell's state.  Every CLI process runs the one loaded Shell-Seg,
 * so the state is the process's own, in pr_ShellPrivate. */
struct sh {
	struct CommandLineInterface *sh_Cli;
	struct Process *sh_Me;
	struct inp *sh_Top;
	int sh_Quit, sh_Once;
	struct inhead sh_Base;
	char sh_Line[LINEMAX + 2], sh_XLine[LINEMAX + 2];
};
#define cli	(S->sh_Cli)
#define me	(S->sh_Me)
#define top	(S->sh_Top)
#define quit	(S->sh_Quit)
#define once	(S->sh_Once)
#define base	(*(struct inp *)&S->sh_Base)
#define line	(S->sh_Line)
#define xline	(S->sh_XLine)

static struct sh *
shget()
{
	return ((struct sh *)((struct Process *)FindTask(NULL))->
	    pr_ShellPrivate);
}

static char version[] = "$VER: Shell 39.2 (14.11.2026)";

static char *bnames[] = {
	"Ask", "CD", "Echo", "Else", "EndCLI", "EndIf", "EndShell", "EndSkip",
	"Execute", "Failat", "Fault", "If", "Lab", "NewCLI", "NewShell",
	"Path", "Prompt", "Quit", "Resident", "Run", "Skip", "Stack", "Why",
	"Set", "Setenv", "Alias", "Get", "Getenv", "Unset", "Unsetenv",
	"Unalias", NULL
};
enum {
	B_ASK, B_CD, B_ECHO, B_ELSE, B_ENDCLI, B_ENDIF, B_ENDSHELL,
	B_ENDSKIP, B_EXECUTE, B_FAILAT, B_FAULT, B_IF, B_LAB, B_NEWCLI,
	B_NEWSHELL, B_PATH, B_PROMPT, B_QUIT, B_RESIDENT, B_RUN, B_SKIP,
	B_STACK, B_WHY, B_SET, B_SETENV, B_ALIAS, B_GET, B_GETENV, B_UNSET,
	B_UNSETENV, B_UNALIAS
};
/* Built-ins that keep the return code, so If can test it again. */
#define CTLMASK	(1L << B_ELSE | 1L << B_ENDIF | 1L << B_ENDSKIP | 1L << B_IF | \
	1L << B_LAB | 1L << B_SKIP)

static int
slen(s)
register char *s;
{
	register int n;

	for (n = 0; s[n] != '\0'; n++)
		;
	return (n);
}

/* a and b equal, ignoring ASCII case. */
static int
seq(a, b)
register char *a, *b;
{
	for (; *a != '\0'; a++, b++)
		if ((*a | 0x20) != (*b | 0x20))
			return (0);
	return (*b == '\0');
}

static void
puts(s)
char *s;
{
	PutStr(s);
}

static void
scopy(d, s, n)
register char *d, *s;
register int n;
{
	while (--n > 0 && *s != '\0')
		*d++ = *s++;
	*d = '\0';
}

/* The next word of *pp into buf (at most WORDMAX bytes): a quoted one
 * with AmigaDOS's * escapes, or one up to a blank; NULL at the end. */
static char *
word(pp, buf)
char **pp;
char *buf;
{
	register char *p, *b;
	register int c;

	for (p = *pp; *p == ' ' || *p == '\t'; p++)
		;
	if (*p == '\0' || *p == '\n') {
		*pp = p;
		return (NULL);
	}
	b = buf;
	if (*p == '"') {
		for (p++; (c = *p) != '\0' && c != '"'; p++) {
			if (c == '*' && p[1] != '\0') {
				c = *++p;
				c = c == 'N' || c == 'n' ? '\n' :
				    c == 'E' || c == 'e' ? 0x1B : c;
			}
			if (b < buf + WORDMAX - 1)
				*b++ = c;
		}
		if (*p == '"')
			p++;
	} else
		for (; *p != '\0' && *p != ' ' && *p != '\t' && *p != '\n'; p++)
			if (b < buf + WORDMAX - 1)
				*b++ = *p;
	*b = '\0';
	*pp = p;
	return (buf);
}

/* The next line of ip into line, without its newline; 0 at the end. */
static int
getline(ip)
register struct inp *ip;
{
	register struct sh *S = shget();
	register int n, c;

	n = 0;
	for (;;) {
		if (ip->i_Pos == ip->i_End) {
			if (ip->i_File == NULL || (ip->i_End = Read(ip->i_File,
			    ip->i_Buf, (LONG)LINEMAX)) <= 0) {
				ip->i_Pos = ip->i_End = 0;
				line[n] = '\0';
				return (n > 0);
			}
			ip->i_Pos = 0;
		}
		c = ip->i_Buf[ip->i_Pos++];
		if (c == '\n')
			break;
		if (n < LINEMAX)
			line[n++] = c;
	}
	line[n] = '\0';
	return (1);
}

static void
setcur()
{
	register struct sh *S = shget();
	cli->cli_CurrentInput = top == &base && once ? NULL : top->i_File;
}

/* End the top script. */
static void
pop()
{
	register struct sh *S = shget();
	register struct inp *ip;

	if ((ip = top) == &base)
		return;
	if (ip->i_Skip == SK_LAB) {
		Printf("Label %s not found by Skip\n", ip->i_Label);
		cli->cli_ReturnCode = RETURN_ERROR;
	}
	top = ip->i_Up;
	Close(ip->i_File);
	FreeVec(ip);
	setcur();
}

static void
popall()
{
	register struct sh *S = shget();
	while (top != &base)
		pop();
}

static void
prompt()
{
	register struct sh *S = shget();
	register char *p;
	char buf[CLI_SETNAME];

	for (p = cli->cli_Prompt; *p != '\0'; p++) {
		if (*p == '%' && (p[1] == 'N' || p[1] == 'n')) {
			Printf("%ld", me->pr_TaskNum);
			p++;
		} else if (*p == '%' && (p[1] == 'S' || p[1] == 's')) {
			GetCurrentDirName(buf, (LONG)sizeof(buf));
			puts(buf);
			p++;
		} else
			WriteChars(p, 1L);
	}
}

static LONG
fault(name)
char *name;
{
	PrintFault(IoErr(), name);
	return (RETURN_ERROR);
}

/* Examine name: its fib_DirEntryType (and *prot its protection), or 0
 * with IoErr set. */
static LONG
exam(name, prot)
char *name;
LONG *prot;
{
	register struct FileInfoBlock *fib;
	BPTR lk;
	LONG t;

	t = 0;
	if ((fib = (struct FileInfoBlock *)AllocDosObject((ULONG)DOS_FIB,
	    (struct TagItem *)NULL)) != NULL &&
	    (lk = Lock(name, SHARED_LOCK)) != NULL) {
		if (Examine(lk, fib) != DOSFALSE) {
			t = fib->fib_DirEntryType;
			*prot = fib->fib_Protection;
		}
		UnLock(lk);
	}
	FreeDosObject((ULONG)DOS_FIB, fib);
	return (t);
}

/* ReadArgs of a built-in's arguments args: NULL, reported, on an
 * error. */
static struct RDArgs *
bargs(tpl, arr, args)
char *tpl, *args;
LONG *arr;
{
	register struct RDArgs *rd;
	char *o;

	o = SetArgStr(args);
	if ((rd = ReadArgs(tpl, arr, (struct RDArgs *)NULL)) == NULL)
		PrintFault(IoErr(), NULL);
	SetArgStr(o);
	return (rd);
}

static LONG
cd(p)
char *p;
{
	char buf[CLI_SETNAME];
	struct RDArgs *rd;
	LONG a[1], t;
	BPTR lk;

	a[0] = 0;
	if ((rd = bargs("DIR", a, p)) == NULL)
		return (RETURN_ERROR);
	if (a[0] == 0) {
		FreeArgs(rd);
		GetCurrentDirName(buf, (LONG)sizeof(buf));
		puts(buf);
		puts("\n");
		return (RETURN_OK);
	}
	scopy(buf, (char *)a[0], (int)sizeof(buf));
	FreeArgs(rd);
	if ((t = exam(buf, &t)) <= 0 || (lk = Lock(buf, SHARED_LOCK)) == NULL) {
		if (t < 0)
			SetIoErr(ERROR_OBJECT_WRONG_TYPE);
		return (fault(buf));
	}
	UnLock(CurrentDir(lk));
	if (NameFromLock(lk, buf, (LONG)sizeof(buf)) != DOSFALSE)
		SetCurrentDirName(buf);
	return (RETURN_OK);
}

/* Echo /M,NOLINE/S,FIRST/K/N,LEN/K/N,TO/K: the words joined by blanks;
 * FIRST starts at that character, LEN takes that many (without FIRST,
 * the last ones). */
static LONG
echo(p)
char *p;
{
	register struct sh *S = shget();
	struct RDArgs *rd;
	LONG a[5], n, f;
	register char *d, *s, **w;
	BPTR out;
	int rc;

	a[0] = a[1] = a[2] = a[3] = a[4] = 0;
	if ((rd = bargs("/M,NOLINE/S,FIRST/K/N,LEN/K/N,TO/K", a, p)) == NULL)
		return (RETURN_ERROR);
	d = xline;
	if ((w = (char **)a[0]) != NULL)
		for (; *w != NULL; w++) {
			if (w != (char **)a[0] && d < xline + LINEMAX)
				*d++ = ' ';
			for (s = *w; *s != '\0' && d < xline + LINEMAX; )
				*d++ = *s++;
		}
	n = d - xline;
	f = a[2] != 0 ? *(LONG *)a[2] - 1 : 0;
	if (a[3] != 0 && a[2] == 0)
		f = n - *(LONG *)a[3];
	if (f < 0)
		f = 0;
	if (f > n)
		f = n;
	n -= f;
	if (a[3] != 0 && *(LONG *)a[3] < n)
		n = *(LONG *)a[3] > 0 ? *(LONG *)a[3] : 0;
	d = xline + (int)f;
	if (!a[1])
		d[(int)n++] = '\n';
	rc = RETURN_OK;
	out = Output();
	if (a[4] != 0 && (out = Open((char *)a[4], MODE_NEWFILE)) == NULL)
		rc = fault((char *)a[4]);
	else if (Write(out, d, n) != n)
		rc = RETURN_ERROR;
	if (out != NULL && out != Output())
		Close(out);
	FreeArgs(rd);
	return (rc);
}

/* Show the variables of type (local, or with GVF_GLOBAL_ONLY those in
 * ENV:). */
static void
vlist(type)
LONG type;
{
	register struct sh *S = shget();
	register struct LocalVar *v;
	register struct FileInfoBlock *fib;
	char buf[LINEMAX];
	BPTR lk;

	if (!(type & GVF_GLOBAL_ONLY)) {
		for (v = (struct LocalVar *)me->pr_LocalVars.mlh_Head;
		    v->lv_Node.ln_Succ != NULL;
		    v = (struct LocalVar *)v->lv_Node.ln_Succ)
			if (v->lv_Node.ln_Type == type) {
				Printf("%-16s", v->lv_Node.ln_Name);
				WriteChars((char *)v->lv_Value, v->lv_Len);
				puts("\n");
			}
		return;
	}
	if ((fib = (struct FileInfoBlock *)AllocDosObject((ULONG)DOS_FIB,
	    (struct TagItem *)NULL)) == NULL)
		return;
	if ((lk = Lock("ENV:", SHARED_LOCK)) != NULL) {
		if (Examine(lk, fib) != DOSFALSE)
			while (ExNext(lk, fib) != DOSFALSE)
				if (fib->fib_DirEntryType < 0 &&
				    GetVar(fib->fib_FileName, buf, (LONG)LINEMAX,
				    GVF_GLOBAL_ONLY) >= 0)
					Printf("%-16s%s\n", fib->fib_FileName, buf);
		UnLock(lk);
	}
	FreeDosObject((ULONG)DOS_FIB, fib);
}

/* Set, Setenv, Alias NAME,STRING/F; Get, Getenv NAME/A; Unset, Unsetenv,
 * Unalias NAME. */
static LONG
var(b, p)
int b;
char *p;
{
	struct RDArgs *rd;
	char buf[LINEMAX];
	LONG a[2], f;
	int rc;

	f = b == B_SETENV || b == B_GETENV || b == B_UNSETENV ? GVF_GLOBAL_ONLY :
	    b == B_ALIAS || b == B_UNALIAS ? (LONG)LV_ALIAS : GVF_LOCAL_ONLY;
	a[0] = a[1] = 0;
	if ((rd = bargs(b == B_GET || b == B_GETENV ? "NAME/A" :
	    b >= B_UNSET ? "NAME" : "NAME,STRING/F", a, p)) == NULL)
		return (RETURN_ERROR);
	rc = RETURN_OK;
	if (b >= B_UNSET) {
		if (a[0] != 0)
			DeleteVar((char *)a[0], f);
	} else if (a[0] == 0)
		vlist(f & ~GVF_LOCAL_ONLY);
	else if (a[1] != 0 && b < B_GET) {
		if (SetVar((char *)a[0], (char *)a[1], -1L, f) == DOSFALSE)
			rc = fault((char *)a[0]);
	} else if (GetVar((char *)a[0], buf, (LONG)LINEMAX, f) >= 0) {
		puts(buf);
		puts("\n");
	} else if (b >= B_GET)
		rc = RETURN_WARN;
	FreeArgs(rd);
	return (rc);
}

/* y gives RETURN_WARN, n or an empty line RETURN_OK; others ask again. */
static LONG
ask(p)
char *p;
{
	char w[WORDMAX], r[WORDMAX];
	register char *q;

	if (word(&p, w) == NULL)
		w[0] = '\0';
	for (;;) {
		puts(w);
		puts(" ");
		if (FGets(Input(), r, (ULONG)sizeof(r)) == NULL)
			return (RETURN_OK);
		for (q = r; *q == ' '; q++)
			;
		if ((*q | 0x20) == 'y')
			return (RETURN_WARN);
		if ((*q | 0x20) == 'n' || *q == '\n' || *q == '\0')
			return (RETURN_OK);
	}
}

/* Show or set a CLI number: Failat, Stack. */
static LONG
number(p, v, min, fmt)
char *p;
LONG *v, min;
char *fmt;
{
	char w[WORDMAX];
	LONG n;

	if (word(&p, w) == NULL) {
		Printf(fmt, *v * (min > 1 ? 4 : 1));
		return (RETURN_OK);
	}
	if (StrToLong(w, &n) < 0) {
		puts("Bad number\n");
		return (RETURN_ERROR);
	}
	if (n < min) {
		puts("Requested size too small\n");
		return (RETURN_ERROR);
	}
	*v = min > 1 ? (n + 3) >> 2 : n;
	return (RETURN_OK);
}

static LONG
xfault(p)
char *p;
{
	char w[WORDMAX], buf[80];
	LONG n;

	while (word(&p, w) != NULL)
		if (StrToLong(w, &n) >= 0) {
			Fault(n, NULL, buf, (LONG)sizeof(buf));
			Printf("Fault %ld: %s\n", n, buf);
		}
	return (RETURN_OK);
}

static LONG
why()
{
	register struct sh *S = shget();
	char buf[80];

	if (cli->cli_ReturnCode == 0 || cli->cli_Result2 == 0)
		puts("The last command did not set a return code\n");
	else {
		Fault(cli->cli_Result2, NULL, buf, (LONG)sizeof(buf));
		Printf("Last command failed because %s\n", buf);
	}
	return (RETURN_OK);
}

/* If: start skipping to Else or EndIf when the condition is false. */
static LONG
xif(p)
char *p;
{
	register struct sh *S = shget();
	char w[WORDMAX], a[WORDMAX], b[WORDMAX];
	register int c, not, val, op;
	register LONG rc;
	LONG x, y;
	BPTR lk;

	c = not = val = op = 0;
	a[0] = b[0] = '\0';
	rc = cli->cli_ReturnCode;
	while (word(&p, w) != NULL) {
		if (seq("NOT", w))
			not = 1;
		else if (seq("WARN", w))
			c = rc >= RETURN_WARN;
		else if (seq("ERROR", w))
			c = rc >= RETURN_ERROR;
		else if (seq("FAIL", w))
			c = rc >= RETURN_FAIL;
		else if (seq("VAL", w))
			val = 1;
		else if (seq("EXISTS", w)) {
			if (word(&p, w) != NULL && (lk = Lock(w, SHARED_LOCK))
			    != NULL) {
				UnLock(lk);
				c = 1;
			} else
				c = 0;
		} else if (seq("EQ", w) || seq("GT", w) || seq("GE", w)) {
			op = (w[1] | 0x20) == 'q' ? 1 : (w[1] | 0x20) == 't' ? 2 : 3;
			if (word(&p, b) == NULL)
				b[0] = '\0';
		} else
			scopy(a, w, WORDMAX);
	}
	if (op) {
		if (val && StrToLong(a, &x) >= 0 && StrToLong(b, &y) >= 0)
			x -= y;
		else
			for (x = 0, c = 0; x == 0; c++) {
				x = (a[c] | (a[c] >= 'A' && a[c] <= 'Z' ? 0x20 : 0)) -
				    (b[c] | (b[c] >= 'A' && b[c] <= 'Z' ? 0x20 : 0));
				if (a[c] == '\0')
					break;
			}
		c = op == 1 ? x == 0 : op == 2 ? x > 0 : x >= 0;
	}
	if (c == not) {
		top->i_Skip = SK_IF;
		top->i_Depth = 0;
	}
	return (RETURN_OK);
}

static LONG
skip(p)
char *p;
{
	register struct sh *S = shget();
	char w[WORDMAX];
	register struct inp *ip;

	if ((ip = top) == &base) {
		puts("Skip must be in a command file\n");
		return (RETURN_ERROR);
	}
	ip->i_Label[0] = '\0';
	while (word(&p, w) != NULL)
		if (seq("BACK", w)) {
			Seek(ip->i_File, 0L, OFFSET_BEGINNING);
			ip->i_Pos = ip->i_End = 0;
		} else
			scopy(ip->i_Label, w, WORDMAX);
	ip->i_Skip = SK_LAB;
	return (RETURN_OK);
}

/* A skipped line: does it end the skip? */
static void
skipline(ip, p)
register struct inp *ip;
char *p;
{
	char w[WORDMAX], l[WORDMAX];

	if (word(&p, w) == NULL)
		return;
	if (ip->i_Skip == SK_LAB) {
		if (ip->i_Label[0] == '\0' ? seq("EndSkip", w) : seq("Lab", w) &&
		    word(&p, l) != NULL && seq(ip->i_Label, l))
			ip->i_Skip = SK_NONE;
	} else if (seq("If", w))
		ip->i_Depth++;
	else if (seq("EndIf", w)) {
		if (ip->i_Depth-- == 0)
			ip->i_Skip = SK_NONE;
	} else if (seq("Else", w) && ip->i_Depth == 0 && ip->i_Skip == SK_IF)
		ip->i_Skip = SK_NONE;
}

/* Copy s into ip's pool: the copy, or NULL without room. */
static char *
pool(ip, s)
register struct inp *ip;
char *s;
{
	register char *d;
	register int n;

	n = slen(s) + 1;
	if (ip->i_Fill + n > POOL)
		return (NULL);
	d = ip->i_Pool + ip->i_Fill;
	ip->i_Fill += n;
	scopy(d, s, n);
	return (d);
}

static int
key(ip, name)
register struct inp *ip;
char *name;
{
	register int k;

	for (k = 0; k < ip->i_NKeys; k++)
		if (seq(ip->i_Name[k], name))
			return (k);
	return (-1);
}

/* .key: parse the template t and bind the script's arguments to it. */
static int
bind(ip, t)
register struct inp *ip;
char *t;
{
	char w[WORDMAX];
	register char *q;
	register int k, n;
	char *p;

	if (ip->i_Keyed++ || (t = pool(ip, t)) == NULL)
		return (1);
	for (n = 0; *t != '\0' && n < NKEYS; n++) {
		while (*t == ' ')
			t++;
		ip->i_Name[n] = t;
		for (; *t != '\0' && *t != ',' && *t != ' '; t++)
			if (*t == '/') {
				*t = '\0';
				q = t + 1;
				ip->i_Flag[n] |= (*q | 0x20) == 'a' ? KF_A :
				    (*q | 0x20) == 'k' ? KF_K :
				    (*q | 0x20) == 's' ? KF_S : 0;
			}
		while (*t == ' ')
			*t++ = '\0';
		if (*t == ',')
			*t++ = '\0';
	}
	ip->i_NKeys = n;
	p = ip->i_Args;
	while (word(&p, w) != NULL) {
		if ((k = key(ip, w)) >= 0 && ip->i_Val[k] == NULL) {
			if (ip->i_Flag[k] & KF_S) {
				ip->i_Val[k] = ip->i_Name[k];
				continue;
			}
			if (word(&p, w) == NULL)
				return (0);
		} else
			for (k = 0; k < n && (ip->i_Val[k] != NULL ||
			    (ip->i_Flag[k] & (KF_K | KF_S))); k++)
				;
		if (k == n || (ip->i_Val[k] = pool(ip, w)) == NULL)
			return (0);
	}
	for (k = 0; k < n; k++)
		if ((ip->i_Flag[k] & KF_A) && ip->i_Val[k] == NULL)
			return (0);
	return (1);
}

/* A dot line of script ip: 0 when its arguments do not fit .key. */
static int
dot(ip, p)
register struct inp *ip;
char *p;
{
	char w[WORDMAX], v[WORDMAX];
	register int k;

	p++;
	if (*p == ' ' || word(&p, w) == NULL)
		return (1);
	while (*p == ' ')
		p++;
	if (seq("key", w) || seq("k", w))
		return (bind(ip, p));
	if (seq("bra", w))
		ip->i_Bra = *p;
	else if (seq("ket", w))
		ip->i_Ket = *p;
	else if (seq("dollar", w) || seq("dol", w))
		ip->i_Dol = *p;
	else if ((seq("default", w) || seq("def", w)) && word(&p, w) != NULL &&
	    word(&p, v) != NULL && (k = key(ip, w)) >= 0)
		ip->i_Def[k] = pool(ip, v);
	return (1);
}

/* Replace <key>, <key$default> and <$$> in src into xline. */
static char *
subst(ip, src)
register struct inp *ip;
char *src;
{
	register struct sh *S = shget();
	register char *s, *d, *e, *v;
	char name[WORDMAX], *def, num[12];
	register int k;

	d = xline;
	for (s = src; *s != '\0' && d < xline + LINEMAX; ) {
		if (*s != ip->i_Bra) {
			*d++ = *s++;
			continue;
		}
		for (e = s + 1; *e != '\0' && *e != ip->i_Ket; e++)
			;
		k = e - s - 1;
		if (*e == '\0' || k >= WORDMAX) {
			*d++ = *s++;
			continue;
		}
		scopy(name, s + 1, k + 1);
		v = NULL;
		if (name[0] == '$' && name[1] == '$' && name[2] == '\0') {
			for (k = me->pr_TaskNum, v = num + 11, *v = '\0'; ; k /= 10) {
				*--v = '0' + k % 10;
				if (k < 10)
					break;
			}
		} else {
			for (def = name; *def != '\0' && *def != ip->i_Dol; def++)
				;
			if (*def != '\0')
				*def++ = '\0';
			else
				def = NULL;
			if ((k = key(ip, name)) >= 0) {
				v = ip->i_Val[k];
				if (v == NULL)
					v = def != NULL ? def : ip->i_Def[k];
				if (v == NULL)
					v = "";
			}
		}
		if (v == NULL) {
			*d++ = *s++;
			continue;
		}
		while (*v != '\0' && d < xline + LINEMAX)
			*d++ = *v++;
		s = e + 1;
	}
	*d = '\0';
	return (xline);
}

static LONG
execute(p)
char *p;
{
	register struct sh *S = shget();
	char w[WORDMAX];
	register struct inp *ip;
	BPTR fh;

	if (word(&p, w) == NULL) {
		puts("Execute: no file\n");
		return (RETURN_ERROR);
	}
	if ((fh = Open(w, MODE_OLDFILE)) == NULL)
		return (fault(w));
	if ((ip = (struct inp *)AllocVec((ULONG)sizeof(*ip),
	    MEMF_ANY | MEMF_CLEAR)) == NULL) {
		Close(fh);
		PrintFault(ERROR_NO_FREE_STORE, "Execute");
		return (RETURN_FAIL);
	}
	ip->i_File = fh;
	ip->i_Bra = '<';
	ip->i_Ket = '>';
	ip->i_Dol = '$';
	scopy(ip->i_Args, p, LINEMAX);
	ip->i_Up = top;
	top = ip;
	setcur();
	return (RETURN_OK);
}

/* Path: show, add, remove or reset the command path. */
static LONG
path(p)
char *p;
{
	register struct sh *S = shget();
	char w[WORDMAX], buf[CLI_SETNAME];
	register struct PathNode *n, **pp;
	register int rem, any;
	BPTR lk;

	rem = any = 0;
	for (; word(&p, w) != NULL; ) {
		if (seq("SHOW", w) || seq("QUIET", w) || seq("ADD", w))
			continue;
		if (seq("REMOVE", w)) {
			rem = 1;
			continue;
		}
		if (seq("RESET", w)) {
			for (n = (struct PathNode *)cli->cli_CommandDir; n != NULL;
			    n = (struct PathNode *)cli->cli_CommandDir) {
				cli->cli_CommandDir = n->pn_Next;
				UnLock(n->pn_Lock);
				FreeVec(n);
			}
			any = 1;
			continue;
		}
		any = 1;
		if ((lk = Lock(w, SHARED_LOCK)) == NULL)
			return (fault(w));
		for (pp = (struct PathNode **)&cli->cli_CommandDir; (n = *pp) !=
		    NULL; pp = (struct PathNode **)&n->pn_Next)
			if (rem && SameLock(n->pn_Lock, lk) == LOCK_SAME) {
				*pp = (struct PathNode *)n->pn_Next;
				UnLock(n->pn_Lock);
				FreeVec(n);
				break;
			}
		if (rem)
			UnLock(lk);
		else if ((n = (struct PathNode *)AllocVec((ULONG)sizeof(*n),
		    MEMF_ANY | MEMF_CLEAR)) == NULL) {
			UnLock(lk);
			PrintFault(ERROR_NO_FREE_STORE, "Path");
			return (RETURN_FAIL);
		} else {
			n->pn_Lock = lk;
			*pp = n;
		}
	}
	if (any)
		return (RETURN_OK);
	puts("Current directory\n");
	for (n = (struct PathNode *)cli->cli_CommandDir; n != NULL;
	    n = (struct PathNode *)n->pn_Next)
		if (NameFromLock(n->pn_Lock, buf, (LONG)sizeof(buf)) != DOSFALSE) {
			puts(buf);
			puts("\n");
		}
	puts("C:\n");
	return (RETURN_OK);
}

/* Resident [NAME] [FILE] [REMOVE] [ADD] [REPLACE] [PURE] [SYSTEM]. */
static LONG
resident(p)
char *p;
{
	char w[WORDMAX], name[WORDMAX], file[WORDMAX];
	register struct Segment *s;
	register int rem, add, pure, sys;
	LONG prot;
	BPTR seg;

	name[0] = file[0] = '\0';
	rem = add = pure = sys = 0;
	while (word(&p, w) != NULL)
		if (seq("REMOVE", w))
			rem = 1;
		else if (seq("ADD", w))
			add = 1;
		else if (seq("PURE", w) || seq("FORCE", w))
			pure = 1;
		else if (seq("SYSTEM", w))
			sys = 1;
		else if (!seq("REPLACE", w))
			scopy(name[0] == '\0' ? name : file, w, WORDMAX);
	if (name[0] == '\0') {
		puts("NAME                    USECOUNT\n");
		Forbid();
		for (s = (struct Segment *)((struct DosInfo *)DOSBase->dl_Root->
		    rn_Info)->di_ResList; s != NULL;
		    s = (struct Segment *)s->seg_Next)
			if (s->seg_UC >= 0)
				Printf("%-24s%ld\n", s->seg_Name, s->seg_UC);
			else
				Printf("%-24s%s\n", s->seg_Name,
				    s->seg_UC == CMD_INTERNAL ? "INTERNAL" :
				    s->seg_UC == CMD_DISABLED ? "DISABLED" :
				    "SYSTEM");
		Permit();
		return (RETURN_OK);
	}
	if (file[0] == '\0' && !rem) {
		scopy(file, name, WORDMAX);
		scopy(name, FilePart(file), WORDMAX);
	}
	s = FindSegment(name, (struct Segment *)NULL, 0L);
	if (rem || (s != NULL && !add)) {
		if (s == NULL) {
			Printf("Resident: %s not found\n", name);
			return (RETURN_WARN);
		}
		if (RemSegment(s) == DOSFALSE)
			return (fault(name));
		if (rem)
			return (RETURN_OK);
	}
	if (exam(file, &prot) == 0)
		return (fault(file));
	if (!pure && !(prot & FIBF_PURE)) {
		Printf("%s is not pure - not made resident\n", file);
		return (RETURN_WARN);
	}
	if ((seg = LoadSeg(file)) == NULL)
		return (fault(file));
	if (AddSegment(name, seg, sys ? CMD_SYSTEM : 0L) == DOSFALSE) {
		UnLoadSeg(seg);
		return (fault(name));
	}
	return (RETURN_OK);
}

static LONG
newshell(p)
char *p;
{
	register struct sh *S = shget();
	char w[WORDMAX], win[WORDMAX], from[WORDMAX];
	struct TagItem t[10];
	register struct TagItem *tp;
	BPTR in, out, lk;
	struct MsgPort *con, *old;

	scopy(win, "CON:", WORDMAX);
	scopy(from, "S:Shell-Startup", WORDMAX);
	while (word(&p, w) != NULL)
		if (seq("FROM", w))
			word(&p, from);
		else if (!seq("WINDOW", w))
			scopy(win, w, WORDMAX);
	/* An interactive input is the shell's console, its output "*" on
	 * it, so a window opens once.  The window is not this process's
	 * console. */
	old = GetConsoleTask();
	if ((in = Open(win, MODE_OLDFILE)) == NULL)
		return (fault(win));
	if (IsInteractive(in)) {
		con = ((struct FileHandle *)BADDR(in))->fh_Type;
		SetConsoleTask(con);
		out = Open("*", MODE_NEWFILE);
	} else {
		con = old;
		out = Open(win, MODE_NEWFILE);
	}
	SetConsoleTask(old);
	if (out == NULL) {
		Close(in);
		return (fault(win));
	}
	scopy(xline, "Execute ", LINEMAX);
	if ((lk = Lock(from, SHARED_LOCK)) != NULL) {
		UnLock(lk);
		scopy(xline + 8, from, LINEMAX - 8);
	} else
		xline[0] = '\0';
	tp = t;
	tp->ti_Tag = NP_Seglist;
	(tp++)->ti_Data = (ULONG)DOSBase->dl_Root->rn_ShellSegment;
	tp->ti_Tag = NP_FreeSeglist; (tp++)->ti_Data = 0;
	tp->ti_Tag = NP_Cli; (tp++)->ti_Data = 1;
	tp->ti_Tag = NP_Input; (tp++)->ti_Data = (ULONG)in;
	tp->ti_Tag = NP_Output; (tp++)->ti_Data = (ULONG)out;
	tp->ti_Tag = NP_ConsoleTask; (tp++)->ti_Data = (ULONG)con;
	tp->ti_Tag = NP_Arguments; (tp++)->ti_Data = (ULONG)xline;
	tp->ti_Tag = NP_StackSize; (tp++)->ti_Data = 2048;
	tp->ti_Tag = NP_Name; (tp++)->ti_Data = (ULONG)"Shell Process";
	tp->ti_Tag = TAG_DONE;
	if (CreateNewProc(t) == NULL) {
		Close(out);
		Close(in);
		return (fault("NewShell"));
	}
	return (RETURN_OK);
}

/* The program name names, loaded: the current directory, each Path
 * directory, then C:. */
static BPTR
findcmd(name)
register char *name;
{
	register struct sh *S = shget();
	register char *p;
	register struct PathNode *n;
	char path[WORDMAX + 2];
	BPTR seg, old;

	if ((seg = LoadSeg(name)) != NULL)
		return (seg);
	for (p = name; *p != '\0'; p++)
		if (*p == ':' || *p == '/')
			return (NULL);
	for (n = (struct PathNode *)cli->cli_CommandDir; n != NULL;
	    n = (struct PathNode *)n->pn_Next) {
		old = CurrentDir(n->pn_Lock);
		seg = LoadSeg(name);
		CurrentDir(old);
		if (seg != NULL)
			return (seg);
	}
	path[0] = 'C';
	path[1] = ':';
	scopy(path + 2, name, WORDMAX);
	return (LoadSeg(path));
}

/* Take a ">name" or "<name" word out of args into buf: buf, or NULL. */
static char *
redir(args, c, buf)
char *args;
int c;
char *buf;
{
	register char *p, *q;
	register int k;

	for (p = args; *p != '\0'; p++) {
		if (*p == '"')
			for (p++; *p != '\0' && *p != '"'; p++)
				if (*p == '*' && p[1] != '\0')
					p++;
		if (*p == '\0')
			break;
		if (*p != c || (p != args && p[-1] != ' '))
			continue;
		for (q = p + 1; *q == ' '; q++)
			;
		for (k = 0; *q != '\0' && *q != ' '; q++)
			if (k < WORDMAX - 1)
				buf[k++] = *q;
		buf[k] = '\0';
		for (; *q == ' '; q++)
			;
		while ((*p++ = *q++) != '\0')
			;
		return (buf);
	}
	return (NULL);
}

static LONG
runcmd(name, args)
char *name, *args;
{
	register struct sh *S = shget();
	register struct Segment *s;
	register int n;
	BPTR seg;
	LONG rc;

	Forbid();
	if ((s = FindSegment(name, (struct Segment *)NULL, 0L)) != NULL)
		s->seg_UC++;
	Permit();
	if (s != NULL)
		seg = s->seg_Seg;
	else if ((seg = findcmd(name)) == NULL) {
		Printf("%s: Unknown command\n", name);
		return (RETURN_ERROR);
	}
	n = slen(args);
	args[n++] = '\n';
	args[n] = '\0';
	SetProgramName(name);
	rc = RunCommand(seg, cli->cli_DefaultStack * 4, args, (LONG)n);
	if (rc == -1 && IoErr() == ERROR_NO_FREE_STORE)
		PrintFault(ERROR_NO_FREE_STORE, name);
	if (s != NULL) {
		Forbid();
		s->seg_UC--;
		Permit();
	} else
		UnLoadSeg(seg);
	return (rc);
}

static int
builtin(name)
char *name;
{
	register int i;

	for (i = 0; bnames[i] != NULL; i++)
		if (seq(bnames[i], name))
			return (i);
	return (-1);
}

static LONG
dobuiltin(b, p)
int b;
char *p;
{
	register struct sh *S = shget();
	struct TagItem t[2];
	struct RDArgs *rd;
	LONG n, a[1];

	switch (b) {
	case B_ASK:
		return (ask(p));
	case B_CD:
		return (cd(p));
	case B_ECHO:
		return (echo(p));
	case B_ELSE:
		if (top != &base) {
			top->i_Skip = SK_ELSE;
			top->i_Depth = 0;
		}
		break;
	case B_ENDCLI:
	case B_ENDSHELL:
		quit = 1;
		break;
	case B_EXECUTE:
		return (execute(p));
	case B_FAILAT:
		return (number(p, &cli->cli_FailLevel, 1L, "Fail limit: %ld\n"));
	case B_FAULT:
		return (xfault(p));
	case B_IF:
		return (xif(p));
	case B_NEWCLI:
	case B_NEWSHELL:
		return (newshell(p));
	case B_PATH:
		return (path(p));
	case B_PROMPT:
		a[0] = (LONG)"%N> ";
		if ((rd = bargs("PROMPT", a, p)) == NULL)
			return (RETURN_ERROR);
		SetPrompt((char *)a[0]);
		FreeArgs(rd);
		break;
	case B_QUIT:
		popall();
		return (StrToLong(p, &n) >= 0 ? n : RETURN_OK);
	case B_RESIDENT:
		return (resident(p));
	case B_RUN:
		t[0].ti_Tag = SYS_Asynch;
		t[0].ti_Data = 1;
		t[1].ti_Tag = TAG_DONE;
		if (SystemTagList(p, t) < 0)
			return (fault("Run"));
		break;
	case B_SKIP:
		return (skip(p));
	case B_STACK:
		return (number(p, &cli->cli_DefaultStack, MINSTK,
		    "Current stack size is %ld bytes\n"));
	case B_WHY:
		return (why());
	default:
		if (b >= B_SET)
			return (var(b, p));
	}
	return (RETURN_OK);
}

/* The line buffer p is not in. */
static char *
other(p)
char *p;
{
	register struct sh *S = shget();

	return (p >= line && p < line + sizeof(line) ? xline : line);
}

/* $name and ${name} in p replaced by the variables' values (*$ is $). */
static char *
dollar(p)
char *p;
{
	char name[WORDMAX];
	register char *s, *d, *e, *n;
	char *buf, *o;
	LONG k;

	for (s = p; *s != '\0' && *s != '$'; s++)
		;
	if (*s == '\0')
		return (p);
	d = buf = other(p);
	e = buf + LINEMAX;
	for (s = p; *s != '\0' && d < e; ) {
		if (*s == '*' && s[1] == '$') {
			*d++ = '$';
			s += 2;
			continue;
		}
		if (*s != '$') {
			*d++ = *s++;
			continue;
		}
		n = name;
		o = s;
		k = s[1] == '{';
		for (s += 1 + k; (*s >= 'a' && *s <= 'z') || (*s >= 'A' &&
		    *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_' ||
		    (k && *s != '}' && *s != '\0'); s++)
			if (n < name + WORDMAX - 1)
				*n++ = *s;
		*n = '\0';
		if (k && *s == '}')
			s++;
		if (name[0] == '\0' ||
		    (k = GetVar(name, d, (LONG)(e - d), 0L)) < 0)
			while (o < s && d < e)
				*d++ = *o++;
		else
			d += (int)k;
	}
	*d = '\0';
	return (buf);
}

/* An alias's value in place of the command name, [] (or the end) taking
 * the rest of the line args. */
static char *
alias(name, args)
char *name, *args;
{
	char val[LINEMAX];
	register char *s, *d, *e, *a;
	char *buf;
	int put;

	if (GetVar(name, val, (LONG)sizeof(val), (LONG)LV_ALIAS) < 0)
		return (NULL);
	d = buf = other(name);
	e = buf + LINEMAX;
	put = 0;
	for (s = val; *s != '\0' && d < e; )
		if (s[0] == '[' && s[1] == ']' && !put) {
			for (a = args; *a != '\0' && d < e; )
				*d++ = *a++;
			s += 2;
			put = 1;
		} else
			*d++ = *s++;
	if (!put && *args != '\0' && d < e - 1) {
		*d++ = ' ';
		for (a = args; *a != '\0' && d < e; )
			*d++ = *a++;
	}
	*d = '\0';
	return (buf);
}

static void
run(p)
register char *p;
{
	register struct sh *S = shget();
	register char *name;
	register struct inp *ip;
	BPTR in, out;
	char *rn, rbuf[WORDMAX];
	LONG rc;
	int b, al;

	ip = top;
	while (*p == ' ' || *p == '\t')
		p++;
	if (ip->i_Skip != SK_NONE) {
		skipline(ip, p);
		return;
	}
	if (*p == '\0' || *p == ';')
		return;
	if (ip != &base) {
		if (*p == '.') {
			if (!dot(ip, p)) {
				puts("Execute: parameters unsuitable for key\n");
				cli->cli_ReturnCode = RETURN_FAIL;
				popall();
			}
			return;
		}
		p = subst(ip, p);
	}
	p = dollar(p);
	for (al = 0; ; al++) {
		for (name = p; *p != '\0' && *p != ' ' && *p != '\t'; p++)
			;
		if (*p != '\0')
			*p++ = '\0';
		while (*p == ' ' || *p == '\t')
			p++;
		if (al > 0 || (rn = alias(name, p)) == NULL)
			break;
		p = rn;
		while (*p == ' ' || *p == '\t')
			p++;
	}
	b = builtin(name);
	if (FindSegment(name, (struct Segment *)NULL, 0L) != NULL)
		b = -1;
	if (b >= 0 && (1L << b & CTLMASK)) {
		if ((rc = dobuiltin(b, p)) != RETURN_OK)
			cli->cli_ReturnCode = rc;
		return;
	}
	in = cli->cli_StandardInput;
	out = cli->cli_StandardOutput;
	rc = RETURN_OK;
	if (b != B_RUN) {
		if ((rn = redir(p, '>', rbuf)) != NULL &&
		    (out = Open(rn, MODE_NEWFILE)) == NULL)
			rc = fault(rn);
		else if ((rn = redir(p, '<', rbuf)) != NULL &&
		    (in = Open(rn, MODE_OLDFILE)) == NULL)
			rc = fault(rn);
	}
	if (rc == RETURN_OK) {
		me->pr_CIS = in;
		me->pr_COS = cli->cli_CurrentOutput = out;
		SetIoErr(0L);
		rc = b >= 0 ? dobuiltin(b, p) : runcmd(name, p);
		cli->cli_Result2 = IoErr();
		Flush(out);
		me->pr_CIS = cli->cli_StandardInput;
		me->pr_COS = cli->cli_CurrentOutput = cli->cli_StandardOutput;
	}
	if (in != cli->cli_StandardInput)
		Close(in);
	if (out != cli->cli_StandardOutput)
		Close(out);
	cli->cli_ReturnCode = rc;
	if (top != &base) {
		if (rc >= cli->cli_FailLevel) {
			Printf("%s failed returncode %ld\n", name, rc);
			popall();
		} else if (SetSignal(0L, SIGBREAKF_CTRL_D) & SIGBREAKF_CTRL_D) {
			puts("***Break\n");
			popall();
		}
	}
}

int
main(args, len)
char *args;
LONG len;
{
	register struct sh *S;
	register char *p;
	register int i;
	struct Process *pr;

	pr = (struct Process *)FindTask(NULL);
	if (Cli() == NULL || (S = (struct sh *)AllocMem((ULONG)sizeof(*S),
	    MEMF_ANY | MEMF_CLEAR)) == NULL)
		return (RETURN_FAIL);
	pr->pr_ShellPrivate = (ULONG)S;
	cli = Cli();
	me = pr;
	base.i_File = cli->cli_StandardInput;
	once = cli->cli_CurrentInput == NULL;
	top = &base;
	for (p = args, i = 0; *p != '\0' && *p != '\n' && i < LINEMAX; )
		line[i++] = *p++;
	line[i] = '\0';
	run(line);
	while (!quit && cli->cli_CurrentInput != NULL) {
		if (top == &base && !cli->cli_Background &&
		    IsInteractive(base.i_File)) {
			prompt();
			Flush(Output());
		}
		if (getline(top))
			run(line);
		else if (top != &base)
			pop();
		else
			break;
	}
	popall();
	Flush(Output());
	i = cli->cli_ReturnCode;
	pr->pr_ShellPrivate = 0;
	FreeMem(S, (ULONG)sizeof(*S));
	return (i);
}
