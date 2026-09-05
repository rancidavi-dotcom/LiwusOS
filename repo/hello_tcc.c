/* Programa de exemplo usado pelo teste de integração do Tiny C Compiler
 * (TCC) dentro do LiwusOS. Compilado em runtime pelo comando "tcc",
 * gera um ELF estatico que o kernel carrega como task userspace.
 */
#include <unistd.h>

/* Direct syscall (no libc wrapper needed) */
static inline long liw_syscall(long num, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "memory", "rcx", "r11");
    return ret;
}

static void write_str(const char *s) {
    while (*s) {
        char c = *s++;
        liw_syscall(4, 1, (long)&c, 1);
    }
}

static void write_hex(unsigned long v) {
    char buf[18];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        unsigned nib = (v >> (i * 4)) & 0xF;
        buf[17 - i] = nib < 10 ? '0' + nib : 'a' + (nib - 10);
    }
    write_str(buf);
}

int main(int argc, char **argv) {
    (void)argc;
    write_str("Hello World!\n");
    if (argv && argv[0]) {
        write_str("argv[0]=");
        write_str(argv[0]);
    } else {
        write_str("argv[0]=(null)");
    }
    write_str("\n");
    return 0;
}