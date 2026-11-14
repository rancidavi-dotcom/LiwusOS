#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifndef _OFF_T_DEFINED
#define _OFF_T_DEFINED
typedef int64_t off_t;
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef int64_t ssize_t;
#endif

#ifndef _PID_T_DEFINED
#define _PID_T_DEFINED
typedef int32_t pid_t;
#endif

#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED
typedef uint32_t mode_t;
#endif

#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
typedef long time_t;
#endif

#ifndef _SUSECONDS_T_DEFINED
#define _SUSECONDS_T_DEFINED
typedef long suseconds_t;
#endif

typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint64_t nlink_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;

#endif
