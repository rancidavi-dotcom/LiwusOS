#pragma once
#include <stdint.h>

static inline void debug_print(const char* str) {
    int len = 0;
    while (str[len]) len++;
    asm volatile(
        "mov $4, %%rax\n"
        "mov $1, %%rdi\n"
        "mov %0, %%rsi\n"
        "mov %1, %%rdx\n"
        "int $0x80\n"
        :
        : "r"((uint64_t)str), "r"((uint64_t)len)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory");
}
