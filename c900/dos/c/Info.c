/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * C:Info [DEVICE]: the size, use and state of each started filesystem
 * (or the one DEVICE names), then the volumes.
 */
#include "cargs.h"

#define NDEV	12

static struct InfoData id;

static void
show(name)
char *name;
{
	register LONG k, used, free, pct;
	BPTR lk;
	struct DosList *v;
	char *st;

	if ((lk = Lock(name, SHARED_LOCK)) == NULL)
		return;
	if (Info(lk, &id) != DOSFALSE) {
		k = id.id_NumBlocks * (id.id_BytesPerBlock >> 8) >> 2;
		used = id.id_NumBlocksUsed;
		free = id.id_NumBlocks - used;
		pct = id.id_NumBlocks ? used * 100 / id.id_NumBlocks : 100;
		st = id.id_DiskState == ID_WRITE_PROTECTED ? "Read Only " :
		    id.id_DiskState == ID_VALIDATING ? "Validating" :
		    "Read/Write";
		v = (struct DosList *)id.id_VolumeNode;
		Printf("%-8s %6ldK %7ld %7ld %3ld%% %4ld  %s %s\n", name, k, used,
		    free, pct, id.id_NumSoftErrors, st,
		    v != NULL ? v->dol_Name : "");
	}
	UnLock(lk);
}

int
main(args, len)
char *args;
LONG len;
{
	static char dev[NDEV][32];
	struct RDArgs *rd;
	LONG a[1];
	register struct DosList *dl;
	register int i, n;

	a[0] = 0;
	if ((rd = rdargs("DEVICE", a)) == NULL)
		return (RETURN_ERROR);
	PutStr("\nMounted disks:\n");
	PutStr("Unit        Size    Used    Free Full Errs  Status     Name\n");
	if (a[0] != 0) {
		show((char *)a[0]);
		FreeArgs(rd);
		return (RETURN_OK);
	}
	FreeArgs(rd);
	n = 0;
	dl = LockDosList(LDF_DEVICES | LDF_VOLUMES | LDF_READ);
	while ((dl = NextDosEntry(dl, LDF_DEVICES)) != NULL)
		if (dl->dol_Task != NULL && n < NDEV) {
			scopy(dev[n], dl->dol_Name, 30);
			for (i = 0; dev[n][i] != '\0'; i++)
				;
			dev[n][i] = ':';
			dev[n++][i + 1] = '\0';
		}
	UnLockDosList(LDF_DEVICES | LDF_VOLUMES | LDF_READ);
	for (i = n; --i >= 0; )
		if (IsFileSystem(dev[i]) != DOSFALSE)
			show(dev[i]);
	PutStr("\nVolumes available:\n");
	dl = LockDosList(LDF_VOLUMES | LDF_READ);
	while ((dl = NextDosEntry(dl, LDF_VOLUMES)) != NULL)
		Printf("%s%s\n", dl->dol_Name, dl->dol_Task != NULL ?
		    " [Mounted]" : "");
	UnLockDosList(LDF_VOLUMES | LDF_READ);
	return (RETURN_OK);
}
