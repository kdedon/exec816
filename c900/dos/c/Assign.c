/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Assign NAME,TARGET/M,LIST/S,EXISTS/S,DISMOUNT/S,DEFER/S,PATH/S,
 * ADD/S,REMOVE/S,VOLS/S,DIRS/S,DEVICES/S: without a name, list volumes, assigns (each
 * directory of a multi-directory one, a late one as <path>) and
 * devices.  NAME: alone removes the assign; with targets it becomes
 * their multi-directory assign (ADD adds to it, REMOVE takes a target
 * out of it, DEFER makes a late assign, as does PATH); EXISTS tests the
 * name.  DISMOUNT is accepted and ignored.
 */
#include "cargs.h"

#define F_VOLS	1
#define F_DIRS	2
#define F_DEVS	4

static void
lockname(lk)
BPTR lk;
{
	char buf[256];

	if (NameFromLock(lk, buf, (LONG)sizeof(buf)) == DOSFALSE)
		buf[0] = '\0';
	PutStr(buf);
	PutStr("\n");
}

static void
list(what)
int what;
{
	register struct DosList *dl, *h;
	register struct AssignList *al;
	register int n;

	h = LockDosList(LDF_ALL | LDF_READ);
	if (what & F_VOLS) {
		PutStr("Volumes:\n");
		for (dl = h; (dl = NextDosEntry(dl, LDF_VOLUMES)) != NULL; )
			Printf("%s%s\n", dl->dol_Name, dl->dol_Task != NULL ?
			    " [Mounted]" : "");
	}
	if (what & F_DIRS) {
		PutStr("\nDirectories:\n");
		for (dl = h; (dl = NextDosEntry(dl, LDF_ASSIGNS)) != NULL; ) {
			Printf("%-13s", dl->dol_Name);
			if (dl->dol_Type == DLT_DIRECTORY) {
				lockname(dl->dol_Lock);
				for (al = dl->dol_misc.dol_assign.dol_List; al != NULL;
				    al = al->al_Next) {
					PutStr("             + ");
					lockname(al->al_Lock);
				}
			} else
				Printf("<%s>\n", dl->dol_misc.dol_assign.dol_AssignName);
		}
	}
	if (what & F_DEVS) {
		PutStr("\nDevices:\n");
		for (dl = h, n = 0; (dl = NextDosEntry(dl, LDF_DEVICES)) != NULL; )
			Printf(n++ ? " %s" : "%s", dl->dol_Name);
		PutStr("\n");
	}
	UnLockDosList(LDF_ALL | LDF_READ);
}

int
main(args, len)
char *args;
LONG len;
{
	static char name[WORDMAX];
	struct RDArgs *rd;
	LONG a[12];
	register char *p, **t;
	register int what, n, rc;
	struct DosList *dl;
	BPTR lk;

	for (n = 0; n < 12; n++)
		a[n] = 0;
	if ((rd = rdargs(
	    "NAME,TARGET/M,LIST/S,EXISTS/S,DISMOUNT/S,DEFER/S,PATH/S,ADD/S,REMOVE/S,VOLS/S,DIRS/S,DEVICES/S",
	    a)) == NULL)
		return (RETURN_ERROR);
	what = (a[9] ? F_VOLS : 0) | (a[10] ? F_DIRS : 0) |
	    (a[11] ? F_DEVS : 0);
	if (a[0] == 0 || a[2]) {
		list(what && !a[2] ? what : F_VOLS | F_DIRS | F_DEVS);
		FreeArgs(rd);
		return (RETURN_OK);
	}
	scopy(name, (char *)a[0], WORDMAX);
	for (p = name; *p != '\0' && *p != ':'; p++)
		;
	*p = '\0';
	rc = RETURN_OK;
	if (a[3]) {
		dl = FindDosEntry(LockDosList(LDF_ALL | LDF_READ), name, LDF_ALL);
		UnLockDosList(LDF_ALL | LDF_READ);
		if (dl == NULL) {
			Printf("%s: not assigned\n", name);
			rc = RETURN_WARN;
		}
	} else if ((t = (char **)a[1]) == NULL) {
		if (AssignLock(name, (BPTR)NULL) == DOSFALSE)
			rc = fail(name);
	} else
		for (n = 0; *t != NULL; t++, n++) {
			if (a[5] || a[6]) {
				if (AssignLate(name, *t) == DOSFALSE)
					rc = fail(name);
				continue;
			}
			if ((lk = Lock(*t, SHARED_LOCK)) == NULL) {
				Printf("Can't find %s\n", *t);
				rc = RETURN_ERROR;
				continue;
			}
			if (a[8]) {
				if (RemAssignList(name, lk) == DOSFALSE)
					rc = fail(name);
				UnLock(lk);
			} else if ((n > 0 || a[7]) && AssignAdd(name, lk) !=
			    DOSFALSE)
				;
			else if (n == 0 && AssignLock(name, lk) != DOSFALSE)
				;
			else {
				UnLock(lk);
				rc = fail(name);
			}
		}
	FreeArgs(rd);
	return (rc);
}
