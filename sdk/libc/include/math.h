#ifndef LIWLIB_MATH_H
#define LIWLIB_MATH_H

#define HUGE_VAL 1.0e308

double fabs(double x);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double cos(double x);
double sin(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sqrt(double x);
double ldexp(double x, int exp);
double frexp(double x, int *exp);
double exp(double x);
double log(double x);
double log10(double x);
double pow(double x, double y);

#endif
