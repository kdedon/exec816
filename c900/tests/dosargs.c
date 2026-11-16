/* SPDX-License-Identifier: MIT */
/*
 * Argument parsing: ReadItem's words, quotes, escapes and "="; FindArg
 * with aliases; ReadArgs on a CSource and on the command line
 * (GetArgStr) with /A /K /S /T /N /M /F, keyword=value, a required item
 * after /M taking its last word, the error codes, a caller's buffer,
 * and "?", which prompts on Output() and reads the line from Input()
 * (a file here), a second "?" printing RDA_ExtHelp first.
 */
#define TNAME	"dosargs"
#include "dostest.h"
#include <dos/rdargs.h>

static struct RDArgs rda;
static LONG arr[8];
static char item[32];
static UBYTE pbuf[200];

/* ReadArgs of line against tpl into arr (cleared): the RDArgs or NULL. */
static struct RDArgs *
rd(tpl, line)
char *tpl, *line;
{
	register int i;

	for (i = 0; i < 8; i++)
		arr[i] = 0;
	rda.RDA_Source.CS_Buffer = (UBYTE *)line;
	rda.RDA_Source.CS_Length = 0;
	while (line[(int)rda.RDA_Source.CS_Length] != '\0')
		rda.RDA_Source.CS_Length++;
	rda.RDA_Source.CS_CurChr = 0;
	rda.RDA_DAList = 0;
	rda.RDA_Buffer = NULL;
	rda.RDA_Flags = RDAF_NOPROMPT;
	return (ReadArgs(tpl, arr, &rda));
}

/* The /M array's words joined by commas, in item. */
static char *
words(a)
char **a;
{
	register char *d, *s;

	d = item;
	for (; *a != NULL; a++) {
		if (d != item)
			*d++ = ',';
		for (s = *a; *s != '\0'; )
			*d++ = *s++;
	}
	*d = '\0';
	return (item);
}

/* Write s to a new file name. */
static int
put(name, s)
char *name, *s;
{
	BPTR fh;
	LONG n;

	for (n = 0; s[(int)n] != '\0'; n++)
		;
	if ((fh = Open(name, MODE_NEWFILE)) == NULL)
		return (0);
	n = Write(fh, s, n) == n;
	Close(fh);
	return ((int)n);
}

static int
dtest()
{
	struct CSource cs;
	struct RDArgs *r;
	struct Process *me;
	BPTR in, out, oin, oout;
	char *line;
	LONG n;

	line = "  one \"two *\"x*\" y*N\" k=v =\n";
	cs.CS_Buffer = (UBYTE *)line;
	for (cs.CS_Length = 0; line[(int)cs.CS_Length] != '\0'; cs.CS_Length++)
		;
	cs.CS_CurChr = 0;
	CHECK(ReadItem(item, 32L, &cs) == ITEM_UNQUOTED && same(item, "one"),
	    "ReadItem unquoted");
	CHECK(ReadItem(item, 32L, &cs) == ITEM_QUOTED &&
	    same(item, "two \"x\" y\n"), "ReadItem quoted with escapes");
	CHECK(ReadItem(item, 32L, &cs) == ITEM_UNQUOTED && same(item, "k"),
	    "ReadItem ends a word at =");
	CHECK(ReadItem(item, 32L, &cs) == ITEM_EQUAL, "ReadItem =");
	CHECK(ReadItem(item, 32L, &cs) == ITEM_UNQUOTED && same(item, "v"),
	    "ReadItem after =");
	CHECK(ReadItem(item, 32L, &cs) == ITEM_EQUAL, "ReadItem lone =");
	CHECK(ReadItem(item, 32L, &cs) == ITEM_NOTHING &&
	    ReadItem(item, 32L, &cs) == ITEM_NOTHING, "ReadItem at the end");
	cs.CS_CurChr = 2;
	CHECK(ReadItem(item, 3L, &cs) == ITEM_ERROR, "ReadItem overflow");
	cs.CS_Buffer = (UBYTE *)"\"open";
	cs.CS_Length = 5;
	cs.CS_CurChr = 0;
	CHECK(ReadItem(item, 32L, &cs) == ITEM_ERROR, "ReadItem open quote");

	CHECK(FindArg("FROM/A,TO=AS/K,ALL/S", "as") == 1, "FindArg alias");
	CHECK(FindArg("FROM/A,TO=AS/K,ALL/S", "ALL") == 2, "FindArg");
	CHECK(FindArg("FROM/A,TO=AS/K,ALL/S", "AL") == -1, "FindArg prefix");

	CHECK((r = rd("FROM/A,TO/K,ALL/S,N=NUM/N,Q/T", "a to b all num 12 q\n"))
	    != NULL, "ReadArgs keywords");
	CHECK(same((char *)arr[0], "a") && same((char *)arr[1], "b") &&
	    arr[2] == DOSTRUE && *(LONG *)arr[3] == 12 && arr[4] == DOSTRUE,
	    "the keywords' values");
	FreeArgs(r);
	CHECK(rd("FROM/A,TO/K", "TO=x \"y z\"\n") != NULL &&
	    same((char *)arr[0], "y z") && same((char *)arr[1], "x"),
	    "keyword=value and a quoted word");
	FreeArgs(&rda);
	CHECK(rd("FILES/M,DEST/A", "a b c\n") != NULL &&
	    same(words((char **)arr[0]), "a,b") && same((char *)arr[1], "c"),
	    "a required item takes /M's last word");
	FreeArgs(&rda);
	CHECK(rd("NAME,FILES/M/N", "x 1 -2 3\n") != NULL &&
	    *((LONG **)arr[1])[1] == -2 && ((LONG **)arr[1])[3] == NULL,
	    "/M/N");
	FreeArgs(&rda);
	CHECK(rd("CMD/K,REST/F", "CMD run this, \"and\" that  \n") != NULL &&
	    same((char *)arr[0], "run") &&
	    same((char *)arr[1], "this, \"and\" that"), "/F");
	FreeArgs(&rda);
	CHECK(rd("A,B", "\n") != NULL && arr[0] == 0 && arr[1] == 0,
	    "nothing given leaves the array");
	FreeArgs(&rda);
	CHECK(rd("FROM/A,TO/A", "x\n") == NULL &&
	    IoErr() == ERROR_REQUIRED_ARG_MISSING, "a missing /A");
	CHECK(rd("FROM", "x y\n") == NULL && IoErr() == ERROR_TOO_MANY_ARGS,
	    "too many");
	CHECK(rd("N/N", "1x\n") == NULL && IoErr() == ERROR_BAD_NUMBER,
	    "a bad number");
	CHECK(rd("TO/K", "TO\n") == NULL && IoErr() == ERROR_KEY_NEEDS_ARG,
	    "a keyword without its value");
	CHECK(rd("A/X", "\n") == NULL && IoErr() == ERROR_BAD_TEMPLATE,
	    "a bad template");
	CHECK(rd("A", "\"x\n") == NULL && IoErr() == ERROR_UNMATCHED_QUOTES,
	    "an open quote");
	rda.RDA_Source.CS_CurChr = 0;
	rda.RDA_Source.CS_Buffer = (UBYTE *)"word\n";
	rda.RDA_Source.CS_Length = 5;
	rda.RDA_Buffer = pbuf;
	rda.RDA_BufSiz = sizeof(pbuf);
	rda.RDA_Flags = RDAF_NOALLOC;
	CHECK(ReadArgs("X", arr, &rda) == &rda && rda.RDA_DAList == 0 &&
	    (UBYTE *)arr[0] >= pbuf && (UBYTE *)arr[0] < pbuf + sizeof(pbuf),
	    "parsing in RDA_Buffer");
	FreeArgs(&rda);

	/* the command line */
	me = (struct Process *)FindTask(NULL);
	me->pr_Arguments = (UBYTE *)"in out\n";
	CHECK((r = ReadArgs("FROM,TO", arr, (struct RDArgs *)NULL)) != NULL &&
	    same((char *)arr[0], "in") && same((char *)arr[1], "out"),
	    "ReadArgs on GetArgStr");
	FreeArgs(r);

	/* "?" asks on Output() and reads Input() */
	CHECK(put("SYS:qin", "?\nfirst second\n"), "the answer file");
	CHECK((in = Open("SYS:qin", MODE_OLDFILE)) != NULL, "Open qin");
	CHECK((out = Open("SYS:qout", MODE_NEWFILE)) != NULL, "Open qout");
	oin = SelectInput(in);
	oout = SelectOutput(out);
	me->pr_Arguments = (UBYTE *)"?\n";
	CHECK((r = (struct RDArgs *)AllocDosObject((ULONG)DOS_RDARGS,
	    NULL)) != NULL, "AllocDosObject DOS_RDARGS");
	r->RDA_ExtHelp = (UBYTE *)"more help\n";
	arr[0] = arr[1] = 0;
	CHECK(ReadArgs("A,B/A", arr, r) == r && same((char *)arr[0], "first") &&
	    same((char *)arr[1], "second"), "ReadArgs after ?");
	FreeArgs(r);
	FreeDosObject((ULONG)DOS_RDARGS, r);
	SelectInput(oin);
	SelectOutput(oout);
	Close(in);
	CHECK(Close(out) == DOSTRUE && (in = Open("SYS:qout", MODE_OLDFILE))
	    != NULL, "Open the prompt");
	n = Read(in, item, 31L);
	item[n < 0 ? 0 : (int)n] = '\0';
	Close(in);
	CHECK(same(item, "A,B/A: more help\nA,B/A: "), "the prompt");
	DeleteFile("SYS:qin");
	DeleteFile("SYS:qout");
	return (0);
}
