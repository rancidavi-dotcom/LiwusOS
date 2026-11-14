#include "liwus/math.h"
#include "liwus/stdlib.h"

double fabs(double x) {
    return (x < 0.0) ? -x : x;
}

double floor(double x) {
    long long i = (long long)x;
    if (x < 0.0 && x != (double)i) i--;
    return (double)i;
}

double ceil(double x) {
    long long i = (long long)x;
    if (x > 0.0 && x != (double)i) i++;
    return (double)i;
}

double ldexp(double x, int exp) {
    if (exp > 0) {
        while (exp--) x *= 2.0;
    } else if (exp < 0) {
        while (exp++) x /= 2.0;
    }
    return x;
}

long double ldexpl(long double x, int exp) {
    if (exp > 0) {
        while (exp--) x *= 2.0L;
    } else if (exp < 0) {
        while (exp++) x /= 2.0L;
    }
    return x;
}

float strtof(const char *nptr, char **endptr) {
    return (float)strtod(nptr, endptr);
}

long double strtold(const char *nptr, char **endptr) {
    return (long double)strtod(nptr, endptr);
}
