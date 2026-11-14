#ifndef _STDINT_H
#define _STDINT_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long int64_t;
typedef unsigned long uint64_t;

typedef signed char int_least8_t;
typedef unsigned char uint_least8_t;
typedef short int_least16_t;
typedef unsigned short uint_least16_t;
typedef int int_least32_t;
typedef unsigned int uint_least32_t;
typedef long int_least64_t;
typedef unsigned long uint_least64_t;

typedef signed char int_fast8_t;
typedef unsigned char uint_fast8_t;
typedef long int_fast16_t;
typedef unsigned long uint_fast16_t;
typedef long int_fast32_t;
typedef unsigned long uint_fast32_t;
typedef long int_fast64_t;
typedef unsigned long uint_fast64_t;

typedef long intptr_t;
typedef unsigned long uintptr_t;

typedef long intmax_t;
typedef unsigned long uintmax_t;

#ifndef SIZE_MAX
#define SIZE_MAX   18446744073709551615UL
#endif

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255

#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535

#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  2147483647
#define UINT32_MAX 4294967295U

#define INT64_MIN  (-9223372036854775807L - 1)
#define INT64_MAX  9223372036854775807L
#define UINT64_MAX 18446744073709551615UL

/* The LIWUS SDK currently targets x86_64. These limits are required by
 * libraries such as QuickJS to select their pointer-width-safe value layout. */
#ifndef INTPTR_MIN
#define INTPTR_MIN INT64_MIN
#endif
#ifndef INTPTR_MAX
#define INTPTR_MAX INT64_MAX
#endif
#ifndef UINTPTR_MAX
#define UINTPTR_MAX UINT64_MAX
#endif

#endif
