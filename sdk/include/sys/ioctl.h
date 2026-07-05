#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include <sys/cdefs.h>

#define TIOCGWINSZ 0x5413

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int ioctl(int fd, int request, ...);

#endif
