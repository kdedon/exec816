/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * CON: handler over textcon.device unit 0, as AmigaDOS's CON: in cooked
 * mode.  A boot module: its romtag adds the DosList device CON, and DOS
 * starts the handler as a process on first use.  There is one console,
 * so one handler serves every CON: open (Amiga starts one per window);
 * the window specification after "CON:" is ignored.
 *
 * Input is read ahead from textcon.device and edited as it arrives,
 * echoed: printable characters append to the line, Backspace and Delete
 * remove the last one, Ctrl-X and Ctrl-U clear the line, Return (CR or
 * LF; CR LF counts once) ends it with a newline.  Ctrl-\ ends input: on
 * an empty line the next Read returns 0, otherwise the line is passed
 * on without a newline and the Read after it returns 0.  Ctrl-C to
 * Ctrl-F signal SIGBREAKF_CTRL_C to _F to the process that sent the
 * last packet.  Cursor up and down (CSI A, CSI B) step through the
 * last lines entered (512 bytes of them), replacing the line being
 * edited; other escape and CSI sequences are dropped.  A cooked Read
 * returns at most one line.  In raw mode (SetMode 1) bytes
 * pass unedited and unechoed.  Writes go to textcon.device at once.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <devices/textcon.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

#define LINEMAX	256		/* the edited line with its newline */
#define INMAX	512		/* finished input not yet read */
#define HISTMAX	512		/* lines entered, each ending in a NUL */

static struct IOStdReq *rreq, *wreq;
static UBYTE rbuf[32];
static char line[LINEMAX];
static int llen;
static char inbuf[INMAX];
static int ihead, icount;
static UWORD iin, iout;		/* bytes passed on and read, modulo 64 K */
static UWORD eofq[4];		/* where pending ends of input are */
static int neof;
static int raw, esc, lastcr;
static struct List rdq;		/* waiting reads, by their messages */
static struct Task *breaktask;
static char hist[HISTMAX];
static int hlen, hpos;

static void
cwrite(p, n)
char *p;
LONG n;
{
	if (n > 0) {
		wreq->io_Command = CMD_WRITE;
		wreq->io_Data = (APTR)p;
		wreq->io_Length = n;
		DoIO((struct IORequest *)wreq);
	}
}

static void
putin(c)
int c;
{
	if (icount < INMAX) {
		inbuf[(ihead + icount) % INMAX] = c;
		icount++;
		iin++;
	}
}

/* Pass the edited line on; it is dropped whole if it does not fit. */
static void
endline()
{
	register int i;

	if (icount + llen <= INMAX)
		for (i = 0; i < llen; i++)
			putin(line[i]);
	llen = 0;
}

static void
rubout(n)
register int n;
{
	static char bs[] = "\b \b\b \b\b \b\b \b\b \b\b \b\b \b\b \b";
	register int k;

	for (; n > 0; n -= k) {
		k = n > 8 ? 8 : n;
		cwrite(bs, (LONG)k * 3);
		llen -= k;
	}
}

/* Keep the edited line (without its newline) as the newest entry. */
static void
histadd()
{
	register int i, k;

	if (llen == 0 || llen >= HISTMAX)
		return;
	while (hlen + llen + 1 > HISTMAX) {
		for (k = 0; hist[k] != '\0'; k++)
			;
		for (i = k + 1; i < hlen; i++)
			hist[i - k - 1] = hist[i];
		hlen -= k + 1;
	}
	for (i = 0; i < llen; i++)
		hist[hlen++] = line[i];
	hist[hlen++] = '\0';
}

/* Cursor up (up set) or down: the entry before or after hpos. */
static void
histmove(up)
int up;
{
	register int i;

	if (up) {
		if (hpos == 0)
			return;
		for (hpos--; hpos > 0 && hist[hpos - 1] != '\0'; hpos--)
			;
	} else {
		if (hpos == hlen)
			return;
		while (hist[hpos++] != '\0')
			;
	}
	rubout(llen);
	for (i = 0; hpos + i < hlen && hist[hpos + i] != '\0'; i++)
		line[i] = hist[hpos + i];
	llen = i;
	cwrite(line, (LONG)llen);
}

static void
key(c)
register int c;
{
	char ch;

	if (raw) {
		putin(c);
		return;
	}
	if (esc) {
		if (esc == 2 && (c == 'A' || c == 'B'))
			histmove(c == 'A');
		if (esc == 2 ? c >= 0x40 && c <= 0x7E : c != '[')
			esc = 0;
		else
			esc = 2;
		return;
	}
	if (lastcr && c == '\n') {
		lastcr = 0;
		return;
	}
	lastcr = c == '\r';
	switch (c) {
	case 0x1B:
		esc = 1;
		return;
	case 0x9B:
		esc = 2;
		return;
	case '\r':
	case '\n':
		cwrite("\n", 1L);
		histadd();
		hpos = hlen;
		line[llen++] = '\n';
		endline();
		return;
	case 0x08:
	case 0x7F:
		if (llen > 0)
			rubout(1);
		return;
	case 0x15:
	case 0x18:
		rubout(llen);
		return;
	case 0x1C:
		endline();
		if (neof < 4)
			eofq[neof++] = iin;
		return;
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x06:
		if (breaktask != NULL)
			Signal(breaktask, SIGBREAKF_CTRL_C << (c - 3));
		return;
	}
	if ((c < 0x20 && c != '\t') || (c >= 0x80 && c < 0xA0))
		return;
	if (llen < LINEMAX - 1) {
		ch = c;
		line[llen++] = c;
		cwrite(&ch, 1L);
	}
}

/* Answer waiting reads from the finished input. */
static void
serve()
{
	register struct DosPacket *dp;
	register char *p;
	register int n, k;
	int max;

	while (rdq.lh_Head->ln_Succ != NULL &&
	    (icount > 0 || (neof > 0 && eofq[0] == iout))) {
		dp = (struct DosPacket *)RemHead(&rdq)->ln_Name;
		if (neof > 0 && eofq[0] == iout) {
			for (k = 1; k < neof; k++)
				eofq[k - 1] = eofq[k];
			neof--;
			ReplyPkt(dp, 0L, 0L);
			continue;
		}
		max = neof > 0 ? eofq[0] - iout : icount;
		if (dp->dp_Arg3 < max)
			max = dp->dp_Arg3;
		p = (char *)dp->dp_Arg2;
		for (n = 0; n < max; ) {
			k = inbuf[ihead];
			ihead = (ihead + 1) % INMAX;
			icount--;
			p[n++] = k;
			if (k == '\n' && !raw)
				break;
		}
		iout += n;
		ReplyPkt(dp, (LONG)n, 0L);
	}
}

static void
packet(dp)
register struct DosPacket *dp;
{
	register struct FileHandle *fh;
	LONG r1, r2;

	r1 = DOSTRUE;
	r2 = 0;
	breaktask = dp->dp_Port->mp_SigTask;
	switch ((int)dp->dp_Type) {
	case ACTION_FINDINPUT:
	case ACTION_FINDOUTPUT:
	case ACTION_FINDUPDATE:
		fh = (struct FileHandle *)dp->dp_Arg1;
		fh->fh_Port = (struct MsgPort *)DOSTRUE;
		fh->fh_Args = 0;
		break;
	case ACTION_END:
		break;
	case ACTION_READ:
		if (dp->dp_Arg3 <= 0) {
			r1 = 0;
			break;
		}
		AddTail(&rdq, &dp->dp_Link->mn_Node);
		return;
	case ACTION_WRITE:
		cwrite((char *)dp->dp_Arg2, dp->dp_Arg3);
		r1 = dp->dp_Arg3;
		break;
	case ACTION_SCREEN_MODE:
		raw = dp->dp_Arg1 != 0;
		break;
	case ACTION_WAIT_CHAR:
		r1 = icount > 0 || neof > 0 ? DOSTRUE : DOSFALSE;
		break;
	case ACTION_SEEK:
		r1 = -1;
		r2 = ERROR_ACTION_NOT_KNOWN;
		break;
	default:
		r1 = DOSFALSE;
		r2 = ERROR_ACTION_NOT_KNOWN;
	}
	ReplyPkt(dp, r1, r2);
}

int
main()
{
	register struct Process *me;
	register struct DosPacket *dp;
	register struct MsgPort *rport;
	struct Message *m;
	register int i;

	me = (struct Process *)FindTask(NULL);
	dp = WaitPkt();
	NewList(&rdq);
	rport = CreateMsgPort();
	rreq = (struct IOStdReq *)CreateIORequest(rport, sizeof(*rreq));
	wreq = (struct IOStdReq *)CreateIORequest(CreateMsgPort(),
	    sizeof(*wreq));
	if (wreq == NULL || rreq == NULL ||
	    OpenDevice(TEXTCONNAME, CONU_STANDARD, rreq, 0) != 0) {
		ReplyPkt(dp, DOSFALSE, ERROR_NO_FREE_STORE);
		return (0);
	}
	wreq->io_Device = rreq->io_Device;
	wreq->io_Unit = rreq->io_Unit;
	((struct DosList *)dp->dp_Arg3)->dol_Task = &me->pr_MsgPort;
	ReplyPkt(dp, DOSTRUE, 0L);
	rreq->io_Command = CMD_READ;
	rreq->io_Data = (APTR)rbuf;
	rreq->io_Length = sizeof(rbuf);
	SendIO((struct IORequest *)rreq);
	for (;;) {
		Wait(SIGF_DOS | 1L << rport->mp_SigBit);
		if (GetMsg(rport) != NULL) {
			for (i = 0; i < (int)rreq->io_Actual; i++)
				key(rbuf[i]);
			rreq->io_Command = CMD_READ;
			rreq->io_Data = (APTR)rbuf;
			rreq->io_Length = sizeof(rbuf);
			SendIO((struct IORequest *)rreq);
		}
		while ((m = GetMsg(&me->pr_MsgPort)) != NULL)
			packet((struct DosPacket *)m->mn_Node.ln_Name);
		serve();
	}
}

static char conname[] = "con-handler";
static char conid[] = "con-handler 39.1 (Exec900)";

/* The romtag's init, as (0, seglist, SysBase): list the device CON. */
static APTR
coninit(zero, seg, sysbase)
APTR zero, seg, sysbase;
{
	register struct DosList *dl;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 39L)) == NULL)
		return (NULL);
	if ((dl = MakeDosEntry("CON", (LONG)DLT_DEVICE)) == NULL)
		return (NULL);
	dl->dol_misc.dol_handler.dol_Handler = conname;
	dl->dol_misc.dol_handler.dol_StackSize = 1024;
	dl->dol_misc.dol_handler.dol_Priority = 5;
	dl->dol_misc.dol_handler.dol_SegList = (BPTR)seg;
	AddDosEntry(dl);
	return ((APTR)dl);
}

struct Resident contag = {
	RTC_MATCHWORD, &contag, (APTR)(&contag + 1), 0, 39,
	NT_UNKNOWN, 0, conname, conid, (APTR)coninit
};
