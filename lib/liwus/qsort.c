#include "liwus/stdlib.h"

static void swap_bytes(char *a, char *b, size_t size) {
    while (size--) {
        char tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (!base || nmemb < 2 || size == 0 || !compar) return;

    char *base_ptr = (char *)base;

    if (nmemb <= 7) {
        for (size_t x = 1; x < nmemb; x++) {
            for (size_t y = x; y > 0 && compar(base_ptr + (y - 1) * size, base_ptr + y * size) > 0; y--) {
                swap_bytes(base_ptr + (y - 1) * size, base_ptr + y * size, size);
            }
        }
        return;
    }

    size_t i = 0, j = nmemb - 1;
    char *pivot = base_ptr + (nmemb / 2) * size;

    while (i <= j) {
        while (compar(base_ptr + i * size, pivot) < 0) i++;
        while (compar(base_ptr + j * size, pivot) > 0) {
            if (j == 0) break;
            j--;
        }
        if (i <= j) {
            if (i != j) {
                swap_bytes(base_ptr + i * size, base_ptr + j * size, size);
                if (pivot == base_ptr + i * size) pivot = base_ptr + j * size;
                else if (pivot == base_ptr + j * size) pivot = base_ptr + i * size;
            }
            i++;
            if (j == 0) break;
            j--;
        }
    }

    if (j > 0) qsort(base_ptr, j + 1, size, compar);
    if (i < nmemb) qsort(base_ptr + i * size, nmemb - i, size, compar);
}
