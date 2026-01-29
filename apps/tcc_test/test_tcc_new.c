#include <stdio.h>

/* Declare tcc functions */
typedef struct TCCState TCCState;
extern TCCState *tcc_new(void);
extern void tcc_delete(TCCState *);

int main() {
    printf("Testing tcc_new...\n");
    TCCState *s = tcc_new();
    printf("tcc_new returned: %p\n", s);
    if (s) {
        tcc_delete(s);
        printf("tcc_delete done\n");
    }
    return 0;
}