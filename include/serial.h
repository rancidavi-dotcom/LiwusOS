#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

int init_serial();
void write_serial(char a);
void serial_print(const char *str);
void serial_print_hex(uint64_t n);

#endif
