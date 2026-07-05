#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static double expr_val(void);
static const char *expr_pos;

static double expr_num(void) {
    while (*expr_pos == ' ') expr_pos++;
    double v;
    if (*expr_pos == '(') {
        expr_pos++;
        v = expr_val();
        if (*expr_pos == ')') expr_pos++;
    } else if (*expr_pos == '-') {
        expr_pos++;
        v = -expr_num();
    } else {
        char buf[64]; int i = 0;
        while ((*expr_pos >= '0' && *expr_pos <= '9') || *expr_pos == '.') {
            if (i < 63) buf[i++] = *expr_pos;
            expr_pos++;
        }
        buf[i] = '\0';
        v = atof(buf);
    }
    return v;
}

static double expr_mul(void) {
    double v = expr_num();
    while (*expr_pos == ' ' || *expr_pos == '*' || *expr_pos == '/') {
        while (*expr_pos == ' ') expr_pos++;
        char op = *expr_pos;
        if (op != '*' && op != '/') break;
        expr_pos++;
        double r = expr_num();
        if (op == '*') v *= r;
        else if (r != 0) v /= r;
        else v = 0;
    }
    return v;
}

static double expr_val(void) {
    double v = expr_mul();
    while (*expr_pos == ' ' || *expr_pos == '+' || *expr_pos == '-') {
        while (*expr_pos == ' ') expr_pos++;
        char op = *expr_pos;
        if (op != '+' && op != '-') break;
        expr_pos++;
        double r = expr_mul();
        if (op == '+') v += r;
        else v -= r;
    }
    return v;
}

static double eval(const char *s) {
    expr_pos = s;
    return expr_val();
}

static void print_double(double v, char *out, int max) {
    if (v < 0) { *out++ = '-'; v = -v; }
    long long ip = (long long)v;
    double fp = v - (double)ip;
    char tmp[64]; int i = 0;
    if (ip == 0) tmp[i++] = '0';
    else {
        char rev[32]; int r = 0;
        while (ip > 0) { rev[r++] = '0' + (ip % 10); ip /= 10; }
        while (r > 0) tmp[i++] = rev[--r];
    }
    int j;
    for (j = 0; j < i && j < max - 1; j++) out[j] = tmp[j];
    int p = j;
    if (fp > 0.0000001 && p < max - 2) {
        out[p++] = '.';
        for (int k = 0; k < 6 && p < max - 1; k++) {
            fp *= 10; int d = (int)fp;
            out[p++] = '0' + d; fp -= d;
            if (fp < 0.0000001) break;
        }
    }
    out[p] = '\0';
}

int main(void) {
    char buf[128];
    printf("LiwusOS Calculator (TUI)\n");
    printf("Digite expressoes como '2+3*4' ou 'q' para sair.\n\n");
    while (1) {
        printf("calc> ");
        if (!fgets(buf, sizeof(buf), stdin)) break;
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';
        if (len == 0) continue;
        if (buf[0] == 'q' || buf[0] == 'Q') break;
        double r = eval(buf);
        char out[64];
        print_double(r, out, sizeof(out));
        printf("= %s\n\n", out);
    }
    printf("Adeus!\n");
    return 0;
}
