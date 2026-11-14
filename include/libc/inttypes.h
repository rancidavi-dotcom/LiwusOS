#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

#define INT8_C(value) value
#define INT16_C(value) value
#define INT32_C(value) value
#define INT64_C(value) value##L
#define UINT8_C(value) value##U
#define UINT16_C(value) value##U
#define UINT32_C(value) value##U
#define UINT64_C(value) value##UL
#define INTMAX_C(value) value##L
#define UINTMAX_C(value) value##UL

#define PRId8  "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "ld"

#define PRIi8  "i"
#define PRIi16 "i"
#define PRIi32 "i"
#define PRIi64 "li"

#define PRIu8  "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 "lu"

#define PRIx8  "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 "lx"

#define PRIX8  "X"
#define PRIX16 "X"
#define PRIX32 "X"
#define PRIX64 "lX"

#endif
