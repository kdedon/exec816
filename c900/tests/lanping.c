/* SPDX-License-Identifier: MIT */
/*
 * lance.device on the cable (unit 0), as 10.0.9.2 beside a host at
 * 10.0.9.1: ARP for the host, ping it, and answer ARP and ICMP echo
 * requests until the host has pinged three times.  The sim's host
 * (simrun -lanpeer) pings back after the first echo reply and sends
 * request n + 1 only after a correct reply to n.  On the board, a host
 * at 10.0.9.1 on the MiSTer's network interface does the same with
 * `ping -c 3 10.0.9.2' once the first request is seen.
 */
/* LANCE: peer */
#include "lantest.h"

#define ETH_ARP		0x0806L
#define ETH_IP		0x0800L
#define NREAD		4		/* reads queued per type */
#define DEADLINE	3000		/* ticks: 30 s for a person at a host */
#define PINGS		3
#define PINGID		0x4c4e

static UBYTE myip[4] = { 10, 0, 9, 2 };
static UBYTE hostip[4] = { 10, 0, 9, 1 };
static UBYTE station[ADDRLEN], hostmac[ADDRLEN];
static UBYTE bcast[ADDRLEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static UBYTE pkt[1500], rbuf[2 * NREAD][1500];
static struct IOSana2Req *wr, *rd[2 * NREAD];
static int replied, answered, havehost;

static UWORD
get16(p)
register UBYTE *p;
{
	return ((UWORD)(p[0] << 8 | p[1]));
}

static void
put16(p, v)
register UBYTE *p;
UWORD v;
{
	p[0] = v >> 8;
	p[1] = v;
}

/* The Internet checksum of n bytes; 0 over a correct header. */
static UWORD
cksum(p, n)
register UBYTE *p;
register int n;
{
	register ULONG s;

	for (s = 0; n > 1; n -= 2, p += 2)
		s += get16(p);
	if (n)
		s += (UWORD)(*p << 8);
	while (s >> 16)
		s = (s & 0xffff) + (s >> 16);
	return ((UWORD)~s);
}

static void
cp(d, s, n)
register UBYTE *d, *s;
register int n;
{
	while (n-- > 0)
		*d++ = *s++;
}

static int
xmit(cmd, dst, type, n)
int cmd;
UBYTE *dst;
ULONG type;
int n;
{
	lnset(wr, cmd, type, (APTR)pkt, (ULONG)n);
	cp(wr->ios2_DstAddr, dst, ADDRLEN);
	return (DoIO((struct IORequest *)wr));
}

static int
arp(op, tha, tpa)
int op;
UBYTE *tha, *tpa;
{
	static UBYTE hdr[8] = { 0, 1, 8, 0, 6, 4, 0, 0 };

	hdr[7] = op;
	cp(pkt, hdr, 8);
	cp(pkt + 8, station, ADDRLEN);
	cp(pkt + 14, myip, 4);
	cp(pkt + 18, op == 1 ? (UBYTE *)"\0\0\0\0\0\0" : tha, ADDRLEN);
	cp(pkt + 24, tpa, 4);
	return (xmit(op == 1 ? S2_BROADCAST : CMD_WRITE, tha, ETH_ARP, 28));
}

/* An ICMP message of n bytes at pkt + 20 to dst, in an IP datagram. */
static int
ip(mac, dst, n)
UBYTE *mac, *dst;
int n;
{
	static UBYTE hdr[12] = { 0x45, 0, 0, 0, 0, 0, 0, 0, 64, 1, 0, 0 };

	put16(pkt + 22, 0);
	put16(pkt + 22, cksum(pkt + 20, n));
	cp(pkt, hdr, 12);
	put16(pkt + 2, 20 + n);
	cp(pkt + 12, myip, 4);
	cp(pkt + 16, dst, 4);
	put16(pkt + 10, cksum(pkt, 20));
	return (xmit(CMD_WRITE, mac, ETH_IP, 20 + n));
}

static int
ping()
{
	register int i;

	pkt[20] = 8;
	pkt[21] = 0;
	put16(pkt + 24, PINGID);
	put16(pkt + 26, 1);
	for (i = 0; i < 32; i++)
		pkt[28 + i] = 'a' + i;
	return (ip(hostmac, hostip, 40));
}

static char *
doarp(f, n)
register UBYTE *f;
int n;
{
	if (n < 28 || get16(f) != 1 || get16(f + 2) != 0x0800)
		return (NULL);
	if (get16(f + 6) == 1 && lnsame(f + 24, myip, 4)) {
		if (arp(2, f + 8, f + 14) != 0)
			return ("ARP reply");
	} else if (get16(f + 6) == 2 && lnsame(f + 14, hostip, 4) &&
	    !havehost) {
		cp(hostmac, f + 8, ADDRLEN);
		havehost = 1;
		xKPrintf("lanping: host at %x:%x:%x:%x:%x:%x\n", hostmac[0],
		    hostmac[1], hostmac[2], hostmac[3], hostmac[4], hostmac[5]);
		if (ping() != 0)
			return ("echo request");
	}
	return (NULL);
}

static char *
doip(q, f, n)
struct IOSana2Req *q;
register UBYTE *f;
int n;
{
	register int len;

	if (n < 28 || f[0] != 0x45 || f[9] != 1 || !lnsame(f + 16, myip, 4))
		return (NULL);
	len = get16(f + 2);
	if (len > n || len < 28 || cksum(f, 20) != 0 ||
	    cksum(f + 20, len - 20) != 0)
		return ("bad IP or ICMP checksum");
	if (f[20] == 0 && get16(f + 24) == PINGID && get16(f + 26) == 1 &&
	    lnsame(f + 12, hostip, 4)) {
		xKPrintf("lanping: echo reply from the host\n");
		replied = 1;
	} else if (f[20] == 8) {
		xKPrintf("lanping: echo request %d\n", get16(f + 26));
		cp(pkt + 20, f + 20, len - 20);
		pkt[20] = 0;
		if (ip(q->ios2_SrcAddr, f + 12, len - 20) != 0)
			return ("echo reply");
		answered++;
	}
	return (NULL);
}

static void
post(i)
int i;
{
	lnset(rd[i], CMD_READ, i < NREAD ? ETH_ARP : ETH_IP, (APTR)rbuf[i],
	    0L);
	SendIO((struct IORequest *)rd[i]);
}

static char *
run(a)
struct IOSana2Req *a;
{
	register struct IOSana2Req *q;
	register int i;
	struct Message *m;
	char *err;
	ULONG end;

	if (lndo(a, S2_GETSTATIONADDRESS) != 0)
		return ("S2_GETSTATIONADDRESS");
	cp(station, a->ios2_DstAddr, ADDRLEN);
	cp(a->ios2_SrcAddr, station, ADDRLEN);
	if (lndo(a, S2_CONFIGINTERFACE) != 0)
		return ("S2_CONFIGINTERFACE");
	if ((wr = lncopyreq(a)) == NULL)
		return ("CreateIORequest");
	wr->ios2_Req.io_Message.mn_ReplyPort = CreateMsgPort();
	for (i = 0; i < 2 * NREAD; i++) {
		if ((rd[i] = lncopyreq(a)) == NULL)
			return ("CreateIORequest");
		post(i);
	}
	if (arp(1, bcast, hostip) != 0)
		return ("ARP request");
	err = NULL;
	end = TickCount + DEADLINE;
	while (err == NULL && answered < PINGS) {
		if ((m = GetMsg(lnport)) == NULL) {
			if (TickCount >= end)
				return (!havehost ? "no ARP reply" : !replied ?
				    "no echo reply" : "too few echo requests");
			Sleep(1);
			continue;
		}
		for (i = 0; (struct Message *)rd[i] != m; i++)
			;
		q = rd[i];
		if (q->ios2_Req.io_Error != 0)
			return ("CMD_READ");
		if (q->ios2_PacketType == ETH_ARP)
			err = doarp(rbuf[i], (int)q->ios2_DataLength);
		else
			err = doip(q, rbuf[i], (int)q->ios2_DataLength);
		post(i);
	}
	if (err == NULL && !replied)
		err = "no echo reply";
	for (i = 0; i < 2 * NREAD; i++) {
		AbortIO((struct IORequest *)rd[i]);
		WaitIO((struct IORequest *)rd[i]);
	}
	return (err);
}

int
main()
{
	struct IOSana2Req *a;
	char *err;

	if ((a = lnreq(0)) == NULL) {
		xKPrintf("FAIL lanping: CreateIORequest\n");
		return (1);
	}
	if (OpenDevice(LANCENAME, 0L, a, 0L) != 0) {
		xKPrintf("FAIL lanping: OpenDevice\n");
		return (1);
	}
	err = run(a);
	CloseDevice((struct IORequest *)a);
	if (err != NULL) {
		xKPrintf("FAIL lanping: %s\n", err);
		return (1);
	}
	xKPrintf("PASS lanping\n");
	return (0);
}
