/* SPDX-License-Identifier: MIT */
/*
 * C:CTest, a C program on the C library.  "CTest run ..." checks
 * argv, stdin (S:CScript), stderr, printf formats, stdio files on SYS:, malloc, strtol, atoi,
 * qsort, time, errno and strerror, getenv over DOS variables, prints "ctest ok" and exits 3 through two
 * atexit functions; "CTest chdir" changes directory and returns 0;
 * "CTest cwd" returns 0 at the volume's root and 20 elsewhere; "CTest
 * fail" exits 10.  tests/libc.c runs it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <lvo/dos_lib.h>

#define DAT	"SYS:ctest.dat"
#define TMP	"SYS:ctest.tmp"
#define NLINE	50
#define NSLOT	32

extern char **environ;
static int nfail;
static char buf[128];

static void
check(ok, what)
int ok;
char *what;
{
	if (!ok) {
		printf("FAIL %s\n", what);
		nfail++;
	}
}

/* getenv reads DOS variables (a local one here). */
static void
vars()
{
	char *v;

	check(SetVar("CTVAR", "value\nmore", -1L, 0L) != 0 &&
	    (v = getenv("CTVAR")) != NULL && strcmp(v, "value") == 0,
	    "getenv of a variable");
	check(DeleteVar("CTVAR", 0L) != 0 && getenv("CTVAR") == NULL,
	    "getenv after DeleteVar");
	check(strcmp(strerror(ENOSYS), "function not implemented") == 0 &&
	    strcmp(strerror(ENOENT), "no such file or directory") == 0,
	    "strerror");
}

static void
fmt(want, got)
char *want, *got;
{
	if (strcmp(want, got) != 0) {
		printf("FAIL printf: want \"%s\" got \"%s\"\n", want, got);
		nfail++;
	}
}

static void
formats()
{
	fmt("42    -7|3    |00012", sprintf(buf, "%d %5d|%-5d|%05d", 42, -7,
	    3, 12));
	fmt("ff 10 65535 -32768", sprintf(buf, "%x %o %u %d", 255, 8,
	    (unsigned)65535, -32767 - 1));
	fmt("FFFF0001 -70000", sprintf(buf, "%X %D", 0xFFFF0001L, -70000L));
	fmt("123456789 deadbeef 4294967295 -100000", sprintf(buf,
	    "%ld %lx %lu %ld", 123456789L, 0xDEADBEEFL, -1L, -100000L));
	fmt("abc|     right|left  |tru", sprintf(buf, "%s|%10s|%-6s|%.3s",
	    "abc", "right", "left", "truncate"));
	fmt("ok %", sprintf(buf, "%c%c %%", 'o', 'k'));
	fmt("1.500000 -0.25 1.234568e+04", sprintf(buf, "%f %.2f %e", 1.5,
	    -0.25, 12345.678));
}

static void
files()
{
	register FILE *f;
	register int i;
	char want[32];
	struct stat st;
	long size;

	check((f = fopen(DAT, "w")) != NULL, "fopen w");
	if (f == NULL)
		return;
	for (i = 0; i < NLINE; i++)
		fprintf(f, "line %d of the file\n", i);
	check(fclose(f) == 0, "fclose w");
	check((f = fopen(DAT, "r")) != NULL, "fopen r");
	if (f == NULL)
		return;
	size = 0;
	for (i = 0; fgets(buf, sizeof(buf), f) != NULL; i++) {
		sprintf(want, "line %d of the file\n", i);
		check(strcmp(buf, want) == 0, "fgets text");
		size += strlen(buf);
	}
	check(i == NLINE && feof(f), "fgets count");
	fclose(f);
	check((f = fopen(DAT, "a")) != NULL, "fopen a");
	if (f == NULL)
		return;
	fputs("tail\n", f);
	fclose(f);
	size += 5;
	check((f = fopen(DAT, "r")) != NULL, "fopen r again");
	if (f == NULL)
		return;
	check(fseek(f, -5L, 2) == 0, "fseek from the end");
	check(fgets(buf, sizeof(buf), f) != NULL && strcmp(buf, "tail\n") == 0,
	    "append");
	check(ftell(f) == size, "ftell");
	rewind(f);
	check(getc(f) == 'l', "rewind");
	fclose(f);
	check(stat(DAT, &st) == 0 && st.st_size == size &&
	    (st.st_mode & S_IFMT) == S_IFREG, "stat");
	check(rename(DAT, TMP) == 0, "rename");
	check(access(DAT, 0) == -1 && errno == ENOENT, "access after rename");
	check(unlink(TMP) == 0 && access(TMP, 0) == -1, "unlink");
	check(fopen("SYS:no/such", "r") == NULL && errno == ENOENT,
	    "fopen of a missing file");
	check(mkdir("SYS:cdir", 0777) == 0 && stat("SYS:cdir", &st) == 0 &&
	    (st.st_mode & S_IFMT) == S_IFDIR && rmdir("SYS:cdir") == 0,
	    "mkdir and rmdir");
	check(open(DAT, O_RDONLY) == -1 && errno == ENOENT, "open");
	check(fork() == -1 && errno == ENOSYS, "fork");
}

static void
memory()
{
	char *slot[NSLOT];
	unsigned size[NSLOT];
	register char *p;
	register unsigned j;
	unsigned long seed;
	int i, k, bad;

	for (k = 0; k < NSLOT; k++)
		slot[k] = NULL;
	seed = 1;
	bad = 0;
	for (i = 0; i < 2000; i++) {
		seed = seed * 1103515245L + 12345L;
		k = (unsigned)(seed >> 16) % NSLOT;
		if ((p = slot[k]) != NULL) {
			for (j = 0; j < size[k]; j++)
				if (p[j] != (char)k)
					bad++;
			free(p);
		}
		size[k] = (i % 50 == 0) ? 1500 : 1 + (unsigned)(seed >> 20) % 300;
		if ((p = slot[k] = malloc(size[k])) == NULL) {
			bad++;
			continue;
		}
		for (j = 0; j < size[k]; j++)
			p[j] = k;
	}
	for (k = 0; k < NSLOT; k++)
		free(slot[k]);
	check(bad == 0, "malloc churn");
	check((p = malloc((unsigned)60000)) != NULL, "malloc 60000");
	if (p != NULL) {
		for (j = 0; j < 60000; j++)
			p[j] = j * 7;
		for (j = 0, bad = 0; j < 60000; j++)
			if (p[j] != (char)(j * 7))
				bad++;
		check(bad == 0, "60000-byte block");
		free(p);
	}
	check(malloc((unsigned)65501) == NULL, "malloc over a segment");
	check((p = malloc(10)) != NULL, "malloc 10");
	strcpy(p, "grow");
	check((p = realloc(p, 3000)) != NULL && strcmp(p, "grow") == 0,
	    "realloc");
	p[2999] = 1;
	free(p);
}

static int
icmp(a, b)
int *a, *b;
{
	return (*a < *b ? -1 : *a > *b);
}

static int
scmp(a, b)
char **a, **b;
{
	return (strcmp(*a, *b));
}

static void
numbers()
{
	static int v[] = { 9, -3, 700, 0, 42, -32000, 5, 5, 31000, 1 };
	static char *s[] = { "pear", "apple", "fig", "banana" };
	char *e;
	int i;

	check(strtol("  -1234xyz", &e, 10) == -1234L && *e == 'x',
	    "strtol");
	check(strtol("0x7fff", (char **)NULL, 0) == 32767L &&
	    strtol("777", (char **)NULL, 8) == 511L &&
	    strtol("123456789", (char **)NULL, 10) == 123456789L,
	    "strtol bases");
	check(atoi("  42") == 42 && atol("-100000") == -100000L, "atoi");
	qsort((char *)v, 10, sizeof(int), icmp);
	for (i = 1; i < 10; i++)
		check(v[i - 1] <= v[i], "qsort ints");
	qsort((char *)s, 4, sizeof(char *), scmp);
	check(strcmp(s[0], "apple") == 0 && strcmp(s[3], "pear") == 0,
	    "qsort strings");
}

static void
clocks()
{
	time_t t, u;
	struct tm *tm;

	t = time((time_t *)NULL);
	time(&u);
	check(t >= 252460800L && u >= t && u - t < 5, "time");
	tm = gmtime(&t);
	check(tm->tm_year >= 78 && tm->tm_mon < 12 && tm->tm_mday >= 1,
	    "gmtime");
}

static void
dirs()
{
	char *p;
	int n;

	check(getcwd(buf, sizeof(buf)) != NULL && (n = strlen(buf)) > 0 &&
	    buf[n - 1] == ':', "getcwd at the root");
	check(chdir("SYS:C") == 0 && getcwd(buf, sizeof(buf)) != NULL &&
	    (p = strchr(buf, ':')) != NULL && strcmp(p, ":C") == 0,
	    "chdir");
	check(access("CTest", 0) == 0, "a name in the new directory");
	check(chdir("SYS:C/CTest") == -1 && errno == ENOTDIR,
	    "chdir to a file");
}

static void
bye1()
{
	printf("atexit 1\n");
}

static void
bye2()
{
	printf("atexit 2\n");
}

main(argc, argv)
int argc;
char **argv;
{
	char *p;

	if (argc == 2 && strcmp(argv[1], "fail") == 0)
		exit(10);
	if (argc == 2 && strcmp(argv[1], "chdir") == 0)
		return (chdir("SYS:C") == 0 ? 0 : 20);
	if (argc == 2 && strcmp(argv[1], "cwd") == 0)
		return (getcwd(buf, sizeof(buf)) != NULL &&
		    (p = strchr(buf, ':')) != NULL && p[1] == '\0' ? 0 : 20);
	check(argc == 7, "argc");
	if (argc != 7)
		return (20);
	check(strcmp(argv[0], "CTest") == 0, "argv[0]");
	check(strcmp(argv[1], "run") == 0 && strcmp(argv[2], "a") == 0,
	    "plain words");
	check(strcmp(argv[3], "two words") == 0, "a quoted word");
	check(strcmp(argv[4], "q\"uo*te") == 0, "escapes in quotes");
	check(argv[5][0] == '\0' && strcmp(argv[6], "end") == 0 &&
	    argv[7] == NULL, "an empty word");
	check(environ[0] == NULL && getenv("PATH") == NULL, "environment");
	check(fgets(buf, sizeof(buf), stdin) != NULL &&
	    strcmp(buf, "CTest chdir\n") == 0 && !isatty(0), "stdin");
	fputs("stderr ok\n", stderr);
	vars();
	formats();
	files();
	memory();
	numbers();
	clocks();
	dirs();
	atexit(bye1);
	atexit(bye2);
	printf("ctest %s\n", nfail == 0 ? "ok" : "failed");
	exit(nfail == 0 ? 3 : 20);
}
