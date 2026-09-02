#ifndef _SYS_SYSMACROS_H_
#define _SYS_SYSMACROS_H_

/* Minimal stub for BusyBox compatibility. 
 * Avoids pulling in glibc <bits/types.h> which conflicts with newlib types. */

#include <sys/types.h>

static inline unsigned int major(unsigned long long dev) {
    return (unsigned int)((dev >> 8) & 0xff) | (unsigned int)((dev >> 32) & ~0xff);
}

static inline unsigned int minor(unsigned long long dev) {
    return (unsigned int)(dev & 0xff) | (unsigned int)((dev >> 12) & ~0xff);
}

static inline unsigned long long makedev(unsigned int maj, unsigned int min) {
    return ((unsigned long long)maj << 8) | (unsigned long long)min;
}

#endif
