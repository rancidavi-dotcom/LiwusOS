#ifndef LIWUS_SETJMP_H
#define LIWUS_SETJMP_H

typedef unsigned long jmp_buf[8];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
