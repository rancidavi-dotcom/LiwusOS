#ifndef _KERNEL_TERMIOS_H
#define _KERNEL_TERMIOS_H

#include <stdint.h>

#define NCCS 32

typedef uint32_t tcflag_t;
typedef uint8_t cc_t;
typedef uint32_t speed_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
};

#define BRKINT  0x0001
#define ICRNL   0x0002
#define INPCK   0x0004
#define ISTRIP  0x0008
#define IXON    0x0010

#define OPOST   0x0001

#define CS8     0x0030

#define ECHO    0x0001
#define ICANON  0x0002
#define ISIG    0x0004
#define IEXTEN  0x0008

#define VMIN    6
#define VTIME   5

#define TCSANOW     0
#define TCSADRAIN   1
#define TCSAFLUSH   2

#endif
