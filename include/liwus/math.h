#ifndef LIWUS_MATH_H
#define LIWUS_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

double floor(double x);
double ceil(double x);
double fabs(double x);
double ldexp(double x, int exp);
long double ldexpl(long double x, int exp);

float strtof(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);

#ifdef __cplusplus
}
#endif

#endif /* LIWUS_MATH_H */
