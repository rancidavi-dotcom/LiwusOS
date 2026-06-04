#ifndef LIWLIB_SETJMP_H
#define LIWLIB_SETJMP_H

#include <stdint.h>

typedef intptr_t jmp_buf[6];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
