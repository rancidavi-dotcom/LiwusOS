#ifndef _MNTENT_H_
#define _MNTENT_H_

#include <stdio.h>
#include <sys/types.h>

#define MOUNTED "/etc/mtab"
#define MNTTAB "/etc/fstab"

struct mntent {
    char *mnt_fsname;
    char *mnt_dir;
    char *mnt_type;
    char *mnt_opts;
    int mnt_freq;
    int mnt_passno;
};

struct mntent *setmntent(const char *, const char *);
struct mntent *getmntent(FILE *);
int endmntent(FILE *);
struct mntent *getmntent_r(FILE *, struct mntent *, char *, int);
int addmntent(FILE *, const struct mntent *);
char *hasmntopt(const struct mntent *, const char *);

#endif
