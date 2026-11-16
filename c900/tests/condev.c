/* SPDX-License-Identifier: MIT */
/*
 * textcon.device on the serial console: open checks, CMD_WRITE (counted
 * and NUL-terminated) reaching the line, a CMD_READ pending until input
 * arrives and then returning what has been typed, a quick read from the
 * ring, CMD_CLEAR, AbortIO and an unknown command.  console.device,
 * the window console, is not there without Workbench.
 */
/* EMUFLAGS: --input-mark=CONDEV-GO --input=\ia\ib\ic\id\ie\if */
#include <exec/exec.h>
#include <devices/textcon.h>
#include <devices/console.h>

static char buf[16];

static int
cmd(r, c, d, n)
register struct IOStdReq *r;
int c;
char *d;
long n;
{
	r->io_Command = c;
	r->io_Data = (UBYTE *)d;
	r->io_Length = n;
	return (DoIO(r));
}

static char *
run(r)
register struct IOStdReq *r;
{
	int i;

	if (cmd(r, CMD_WRITE, "condev: counted\n", 16L) != 0 ||
	    r->io_Actual != 16)
		return ("counted write");
	if (cmd(r, CMD_WRITE, "condev: terminated\n", -1L) != 0 ||
	    r->io_Actual != 19)
		return ("terminated write");
	if (cmd(r, 99, NULL, 0L) != IOERR_NOCMD)
		return ("unknown command accepted");

	/* Pending until input arrives, then what is there. */
	r->io_Command = CMD_READ;
	r->io_Data = (UBYTE *)buf;
	r->io_Length = 4;
	SendIO(r);
	Sleep(3);
	if (CheckIO(r) != NULL)
		return ("read completed with no input");
	xKPrintf("CONDEV-GO\n");
	for (i = 0; i < 200 && CheckIO(r) == NULL; i++)
		Sleep(1);
	if (WaitIO(r) != 0 || r->io_Actual < 1 || r->io_Actual > 4 ||
	    buf[0] != 'a')
		return ("pending read");
	i = (int)r->io_Actual;

	/* The rest arrives in the ring; a read takes it quick. */
	Sleep(20);
	if (cmd(r, CMD_READ, buf + i, 16L - i) != 0)
		return ("ring read");
	i += (int)r->io_Actual;
	if (i != 6 || buf[1] != 'b' || buf[5] != 'f')
		return ("ring contents");

	/* Nothing left: CMD_CLEAR, then a read stays pending until aborted. */
	if (cmd(r, CMD_CLEAR, NULL, 0L) != 0)
		return ("clear");
	r->io_Command = CMD_READ;
	r->io_Data = (UBYTE *)buf;
	r->io_Length = 4;
	SendIO(r);
	Sleep(2);
	AbortIO(r);
	if (WaitIO(r) != IOERR_ABORTED)
		return ("abort");
	return (NULL);
}

int
main()
{
	struct MsgPort *port;
	struct IOStdReq *r;
	char *why;

	port = CreateMsgPort();
	r = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
	if (OpenDevice(CONSOLENAME, CONU_STANDARD, r, 0) == 0) {
		xKPrintf("FAIL condev: console.device opened\n");
		return (1);
	}
	if (OpenDevice(TEXTCONNAME, 1, r, 0) != IOERR_OPENFAIL) {
		xKPrintf("FAIL condev: unit 1 opened\n");
		return (1);
	}
	if (OpenDevice(TEXTCONNAME, CONU_STANDARD, r, CONFLAG_DEFAULT) != 0) {
		xKPrintf("FAIL condev: open\n");
		return (1);
	}
	why = run(r);
	CloseDevice(r);
	if (why != NULL) {
		xKPrintf("FAIL condev: %s\n", why);
		return (1);
	}
	xKPrintf("PASS condev\n");
	return (0);
}
