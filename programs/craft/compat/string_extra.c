#include <stddef.h>

static int contains(const char *set, int c) {
    for (; *set; ++set) if ((unsigned char)*set == (unsigned char)c) return 1;
    return 0;
}
size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    while (s[n] && contains(accept, s[n])) ++n;
    return n;
}
size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (s[n] && !contains(reject, s[n])) ++n;
    return n;
}

static unsigned int craft_rand_state = 1;
void srand(unsigned int seed) { craft_rand_state = seed ? seed : 1; }
int rand(void) {
    craft_rand_state = craft_rand_state * 1103515245U + 12345U;
    return (int)((craft_rand_state >> 1) & 0x7fffffffU);
}
