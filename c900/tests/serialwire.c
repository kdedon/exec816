/* SPDX-License-Identifier: MIT */
/*
 * serial.device across loopback wires: line 1 (motherboard SCC channel A)
 * and line 3 (the LR board's first SCC, channel A) each echo what they
 * send.  A read queued before the write is filled by that line's receive
 * interrupt, through its own chip's vectors; a long transfer crosses the
 * ring and several pump bursts.  With the LR board fitted, kboot reports
 * lines 2-5 and they open.
 */
/* WIRE: 1 3 */
#include "test.h"
#include "sertest.h"

#define LONGLEN	200

static char out[LONGLEN];
static char in[LONGLEN];

static char *
loop(unit, len)
ULONG unit;
int len;
{
	struct IOExtSer *r, *w;
	struct MsgPort *port;
	int err, i;

	for (i = 0; i < len; i++) {
		out[i] = (char)(' ' + (i * 7 + (int)unit) % 95);
		in[i] = 0;
	}
	if ((r = seropen(unit, SERF_SHARED, &err)) == NULL)
		return ("OpenDevice failed");
	port = r->IOSer.io_Message.mn_ReplyPort;
	w = (struct IOExtSer *)CreateIORequest(port, sizeof(struct IOExtSer));
	if (w == NULL) {
		serclose(r);
		return ("no request");
	}
	*w = *r;
	sersend(r, CMD_READ, in, (ULONG)len);
	sersend(w, CMD_WRITE, out, (ULONG)len);
	if (!serwait(w, 200) || w->IOSer.io_Error != 0 ||
	    w->IOSer.io_Actual != len)
		return ("the write did not complete");
	DeleteIORequest(w);
	if (!serwait(r, 500))
		return ("the echo never arrived");
	err = r->IOSer.io_Error;
	i = r->IOSer.io_Actual;
	serclose(r);
	if (err != 0 || i != len || !sereq(in, out, len))
		return ("the echo differs");
	return (NULL);
}

int
main()
{
	struct IOExtSer *x;
	char *why;
	int err, u;

	why = NULL;
	for (u = 2; u <= 5 && why == NULL; u++)
		if ((x = seropen((ULONG)u, 0, &err)) == NULL)
			why = "an LR line did not open";
		else
			serclose(x);
	if (why == NULL && (why = loop(1L, 16)) == NULL &&
	    (why = loop(3L, 16)) == NULL && (why = loop(1L, LONGLEN)) == NULL)
		why = loop(3L, LONGLEN);
	if (why != NULL) {
		xKPrintf("FAIL serialwire: %s\n", why);
		return (1);
	}
	xKPrintf("PASS serialwire\n");
	return (0);
}
