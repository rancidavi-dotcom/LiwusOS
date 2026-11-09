#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include <stddef.h>

/* Tipos basicos do lwIP */
typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;

/* Byte-order: x86_64 e little endian */
#define BYTE_ORDER LITTLE_ENDIAN

/* Otimizacao de byte-swapping com builtins do GCC */
#define LWIP_PLATFORM_BYTESWAP 1
#define LWIP_PLATFORM_HTONS(x) __builtin_bswap16(x)
#define LWIP_PLATFORM_HTONL(x) __builtin_bswap32(x)

/* Macros para estruturas packed */
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

/* Gerador de numeros pseudo-aleatorios simples para o lwIP */
static inline u32_t lwip_arch_rand(void) {
    extern uint32_t timer_ticks;
    static uint32_t seed = 0x12345678;
    seed = seed * 1103515245 + 12345 + timer_ticks;
    return seed;
}
#define LWIP_RAND() lwip_arch_rand()

/* Format specifiers para printf */
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "lu"

/* lwIP nao deve incluir <ctype.h> do sistema */
#define LWIP_NO_CTYPE_H 1

/* Declaracoes de funcoes C padrao providas pelo kernel */
long strtol(const char *nptr, char **endptr, int base);
int atoi(const char *nptr);

/* Diagnostico e Asserts (silenciosos ou via serial) */
extern void serial_printf(const char *fmt, ...);

#define LWIP_PLATFORM_DIAG(x)   do { /* serial_printf x; */ } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { /* serial_printf("lwIP ASSERT: %s\n", x); */ } while (0)

#endif /* LWIP_ARCH_CC_H */
