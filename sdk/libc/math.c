#include <math.h>

#define LIW_PI 3.14159265358979323846
#define LIW_HALF_PI 1.57079632679489661923

double fabs(double x) { return x < 0.0 ? -x : x; }

double floor(double x) {
  long i = (long)x;
  if ((double)i > x) {
    i--;
  }
  return (double)i;
}

double ceil(double x) {
  long i = (long)x;
  if ((double)i < x) {
    i++;
  }
  return (double)i;
}

double fmod(double x, double y) {
  long q;

  if (y == 0.0) {
    return 0.0;
  }

  q = (long)(x / y);
  return x - ((double)q * y);
}

static double liw_wrap_pi(double x) {
  while (x > LIW_PI) {
    x -= 2.0 * LIW_PI;
  }
  while (x < -LIW_PI) {
    x += 2.0 * LIW_PI;
  }
  return x;
}

double sin(double x) {
  double x2;
  x = liw_wrap_pi(x);
  x2 = x * x;
  return x * (1.0 - x2 / 6.0 + (x2 * x2) / 120.0 -
              (x2 * x2 * x2) / 5040.0);
}

double cos(double x) { return sin(x + LIW_HALF_PI); }

double tan(double x) {
  double s = sin(x);
  double c = sin(x + LIW_HALF_PI);
  if (c > -0.000001 && c < 0.000001) {
    return s >= 0.0 ? 1000000.0 : -1000000.0;
  }
  return s / c;
}

double atan(double x) {
  double ax = fabs(x);
  double result;

  if (ax > 1.0) {
    result = LIW_HALF_PI - (x / (x * x + 0.28));
  } else {
    result = x / (1.0 + 0.28 * x * x);
  }

  return result;
}

double atan2(double y, double x) {
  if (x > 0.0) {
    return atan(y / x);
  }
  if (x < 0.0 && y >= 0.0) {
    return atan(y / x) + LIW_PI;
  }
  if (x < 0.0 && y < 0.0) {
    return atan(y / x) - LIW_PI;
  }
  if (x == 0.0 && y > 0.0) {
    return LIW_HALF_PI;
  }
  if (x == 0.0 && y < 0.0) {
    return -LIW_HALF_PI;
  }
  return 0.0;
}

double asin(double x) { return atan2(x, sqrt(1.0 - x * x)); }

double acos(double x) { return LIW_HALF_PI - asin(x); }

double sqrt(double x) {
  double guess;

  if (x <= 0.0) {
    return 0.0;
  }

  guess = x > 1.0 ? x : 1.0;
  for (int i = 0; i < 16; i++) {
    guess = 0.5 * (guess + x / guess);
  }
  return guess;
}

double ldexp(double x, int exp) {
  while (exp > 0) {
    x *= 2.0;
    exp--;
  }
  while (exp < 0) {
    x *= 0.5;
    exp++;
  }
  return x;
}

double frexp(double x, int *exp) {
  int e = 0;
  double ax = fabs(x);

  if (x == 0.0) {
    if (exp) {
      *exp = 0;
    }
    return 0.0;
  }

  while (ax >= 1.0) {
    x *= 0.5;
    ax *= 0.5;
    e++;
  }
  while (ax < 0.5) {
    x *= 2.0;
    ax *= 2.0;
    e--;
  }

  if (exp) {
    *exp = e;
  }
  return x;
}

double exp(double x) {
  double sum = 1.0;
  double term = 1.0;

  for (int i = 1; i < 24; i++) {
    term *= x / (double)i;
    sum += term;
  }

  return sum;
}

double log(double x) {
  double y = 0.0;

  if (x <= 0.0) {
    return -HUGE_VAL;
  }

  for (int i = 0; i < 20; i++) {
    double ey = exp(y);
    y += 2.0 * (x - ey) / (x + ey);
  }
  return y;
}

double log10(double x) { return log(x) / 2.302585092994046; }

double modf(double x, double *iptr) {
    long i = (long)x;
    *iptr = (double)i;
    return x - *iptr;
}

double pow(double x, double y) {
  long yi = (long)y;

  if ((double)yi == y) {
    double result = 1.0;
    int neg = yi < 0;
    if (neg) {
      yi = -yi;
    }
    while (yi > 0) {
      if (yi & 1L) {
        result *= x;
      }
      x *= x;
      yi >>= 1;
    }
    return neg ? 1.0 / result : result;
  }

  if (x <= 0.0) {
    return 0.0;
  }

  return exp(log(x) * y);
}
