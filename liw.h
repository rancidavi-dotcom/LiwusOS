#ifndef LIW_H
#define LIW_H

#include <stdint.h>

typedef struct {
    char name[32];
    char version[16];
    uint32_t size;
    uint8_t installed;
} liw_pkg_t;

void liw_init();
void liw_install(const char* name);
void liw_list();

#endif
