#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

/*
 * LiwusOS Test Framework
 *
 * Macros de teste para kernel-side (serial_print) e userspace (printf).
 * Define: KERNEL_TEST para compilar como kernel task.
 */

#ifdef KERNEL_TEST

#include "serial.h"
#include "string.h"

#define TEST_BEGIN(name) do { \
    serial_print("[TEST] "); serial_print(name); serial_print("\n"); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        serial_print("  ASSERT FAIL: "); serial_print(msg); serial_print("\n"); \
        return 1; \
    } \
} while(0)

#define PASS(name) do { \
    serial_print("  PASS: "); serial_print(name); serial_print("\n"); \
    return 0; \
} while(0)

#define FAIL(name, msg) do { \
    serial_print("  FAIL: "); serial_print(name); \
    serial_print(" - "); serial_print(msg); serial_print("\n"); \
    return 1; \
} while(0)

#define TEST_RUNNER_BEGIN serial_print("=== TEST RUNNER START ===\n");
#define TEST_RUNNER_END   serial_print("=== TEST RUNNER END ===\n");

#define TEST_RESULT(pass, fail) do { \
    char _buf[16]; \
    serial_print("RESULT: "); \
    itoa(pass, _buf, 10); serial_print(_buf); \
    serial_print("/"); \
    itoa(pass + fail, _buf, 10); serial_print(_buf); \
    serial_print(" PASS\n"); \
    if (fail > 0) serial_print("RESULT: FAIL\n"); \
    else serial_print("RESULT: ALL PASS\n"); \
} while(0)

#else /* Userspace */

#include <stdio.h>

#define TEST_BEGIN(name) do { fflush(stdout); printf("[TEST] %s\n", name); fflush(stdout); } while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  ASSERT FAIL: %s\n", msg); \
        fflush(stdout); \
        return 1; \
    } \
} while(0)

#define PASS(name) do { \
    printf("  PASS: %s\n", name); \
    fflush(stdout); \
    return 0; \
} while(0)

#define FAIL(name, msg) do { \
    printf("  FAIL: %s - %s\n", name, msg); \
    fflush(stdout); \
    return 1; \
} while(0)

#define TEST_RUNNER_BEGIN do { fflush(stdout); printf("=== TEST RUNNER START ===\n"); fflush(stdout); } while(0)
#define TEST_RUNNER_END   do { fflush(stdout); printf("=== TEST RUNNER END ===\n"); fflush(stdout); } while(0)

#define TEST_RESULT(pass, fail) do { \
    fflush(stdout); \
    printf("RESULT: %d/%d PASS\n", pass, pass + fail); \
    if (fail > 0) printf("RESULT: FAIL\n"); \
    else printf("RESULT: ALL PASS\n"); \
    fflush(stdout); \
} while(0)

#endif

#endif /* TEST_FRAMEWORK_H */
