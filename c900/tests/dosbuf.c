/* SPDX-License-Identifier: MIT */
/*
 * Buffered DOS I/O on the boot volume: FPutC, FPuts, FWrite, VFPrintf
 * and VFWritef into a file, mixed with Write and Seek; reading it back
 * with FGetC, FGets, FRead and UnGetC (also after the end), mixed with
 * Read and Seek; SetVBuf's BUF_NONE, BUF_LINE and a caller's buffer,
 * Flush, and NIL:.
 */
#define TNAME	"dosbuf"
#include "dostest.h"
#include <dos/stdio.h>

static struct FileInfoBlock fib;
static char buf[700];
static char ubuf[16];

#define BYTE(i)	((int)(((i) * 7) & 0xFF))

/* The size the handler has for fh. */
static LONG
hsize(fh)
BPTR fh;
{
	return (ExamineFH(fh, &fib) != DOSFALSE ? fib.fib_Size : -1L);
}

static int
dtest()
{
	BPTR fh;
	LONG a[3];
	int i;

	DeleteFile("SYS:bt");
	CHECK((fh = Open("SYS:bt", MODE_NEWFILE)) != NULL, "Open new");
	for (i = 0; i < 1000; i++)
		CHECK(FPutC(fh, (LONG)BYTE(i)) == BYTE(i), "FPutC");
	CHECK(hsize(fh) < 1000, "FPutC is buffered");
	CHECK(FPuts(fh, "line one\n") == 0, "FPuts");
	CHECK(FWrite(fh, "abcdef", 2L, 3L) == 3, "FWrite");
	a[0] = 42;
	a[1] = (LONG)"str";
	CHECK(VFPrintf(fh, "%ld %s\n", (APTR)a) == 7, "VFPrintf");
	a[0] = (LONG)"bc";
	a[1] = 255;
	a[2] = -5;
	CHECK(VFWritef(fh, "S=%S X=%X4 N=%N!", a) == 17, "VFWritef");
	CHECK(Write(fh, "W", 1L) == 1, "Write after buffered output");
	CHECK(FPutC(fh, (LONG)'x') == 'x', "FPutC x");
	CHECK(Seek(fh, 0L, OFFSET_CURRENT) == 1041, "Seek flushes output");
	CHECK(hsize(fh) == 1041, "the size after Seek");
	CHECK(Close(fh) == DOSTRUE, "Close");

	CHECK((fh = Open("SYS:bt", MODE_OLDFILE)) != NULL, "Open old");
	for (i = 0; i < 1000; i++)
		CHECK(FGetC(fh) == BYTE(i), "FGetC");
	CHECK(FGets(fh, buf, 100L) == buf && same(buf, "line one\n"),
	    "FGets");
	CHECK(Seek(fh, 0L, OFFSET_CURRENT) == 1009, "Seek with read-ahead");
	CHECK(FRead(fh, buf, 3L, 2L) == 2 && buf[0] == 'a' && buf[5] == 'f',
	    "FRead");
	CHECK(UnGetC(fh, -1L) == 'f' && FGetC(fh) == 'f', "UnGetC(-1)");
	CHECK(UnGetC(fh, (LONG)'Z') == 'Z' && FGetC(fh) == 'Z', "UnGetC(c)");
	CHECK(Read(fh, buf, 7L) == 7 && buf[0] == '4' && buf[6] == '\n',
	    "Read after buffered input");
	CHECK(FGets(fh, buf, 100L) != NULL && same(buf, "S=bc X=00ff N=-5!Wx"),
	    "VFWritef's output");
	CHECK(FGetC(fh) == -1 && UnGetC(fh, -1L) == -1 && FGetC(fh) == -1,
	    "the end, pushed back");
	CHECK(FGets(fh, buf, 100L) == NULL, "FGets at the end");
	CHECK(Seek(fh, 0L, OFFSET_BEGINNING) == 1041, "Seek to the start");
	CHECK(FRead(fh, buf, 1L, 600L) == 600, "a long FRead");
	for (i = 0; i < 600; i++)
		CHECK((buf[i] & 0xFF) == BYTE(i), "the long FRead's bytes");
	CHECK(FGetC(fh) == BYTE(600), "FGetC after a long FRead");
	CHECK(Close(fh) == DOSTRUE, "Close old");

	CHECK((fh = Open("SYS:bt", MODE_NEWFILE)) != NULL, "Open again");
	CHECK(SetVBuf(fh, NULL, BUF_NONE, 0L) == 0, "SetVBuf BUF_NONE");
	CHECK(FPutC(fh, (LONG)'a') == 'a' && hsize(fh) == 1, "BUF_NONE");
	CHECK(SetVBuf(fh, NULL, BUF_LINE, 0L) == 0, "SetVBuf BUF_LINE");
	CHECK(FPuts(fh, "bc") == 0 && hsize(fh) == 1, "BUF_LINE holds");
	CHECK(FPuts(fh, "d\ne") == 0 && hsize(fh) == 6, "BUF_LINE at newline");
	CHECK(SetVBuf(fh, ubuf, BUF_FULL, (LONG)sizeof(ubuf)) == 0 &&
	    hsize(fh) == 6, "SetVBuf flushes");
	CHECK(FPuts(fh, "0123456789abcdefgh") == 0 && hsize(fh) == 24,
	    "a block past the buffer goes straight out");
	CHECK(FPutC(fh, (LONG)'-') == '-' && FPuts(fh, "0123456789abcdefgh")
	    == 0 && hsize(fh) == 40, "a 16-byte buffer");
	CHECK(Flush(fh) == DOSTRUE && hsize(fh) == 43, "Flush");
	CHECK(Close(fh) == DOSTRUE, "Close with a caller's buffer");
	CHECK(DeleteFile("SYS:bt") == DOSTRUE, "DeleteFile");

	CHECK((fh = Open("NIL:", MODE_NEWFILE)) != NULL, "Open NIL:");
	CHECK(FPuts(fh, "gone\n") == 0 && FGetC(fh) == -1, "NIL:");
	Close(fh);
	return (0);
}
