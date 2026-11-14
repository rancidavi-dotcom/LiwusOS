#include "liwus/stdio.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/syscall.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("=== LiwusOS System Benchmark (Ring 3 x86_64) ===\n\n");

    /* 1. Benchmark de CPU: Crivo de Numeros Primos + Hash FNV-1a */
    uint64_t t0 = liw_timer_ticks();
    uint32_t primes = 0;
    uint32_t hash = 2166136261U;
    for (uint32_t n = 2; n < 45000U; n++) {
        int is_p = 1;
        for (uint32_t d = 2; d * d <= n; d++) {
            if ((n % d) == 0) {
                is_p = 0;
                break;
            }
        }
        if (is_p) {
            primes++;
            hash = (hash ^ n) * 16777619U;
        }
    }
    uint64_t t1 = liw_timer_ticks();
    uint32_t cpu_ms = (uint32_t)((t1 - t0) * 10ULL);
    if (cpu_ms == 0) cpu_ms = 1;

    printf("[CPU] Primos ate 45000: %u encontrados (hash=0x%x) em %u ms\n",
           primes, hash, cpu_ms);

    /* 2. Benchmark de Memoria (Heap + Copy) */
    uint64_t m0 = liw_timer_ticks();
    size_t buf_sz = 64 * 1024;
    uint8_t *src = (uint8_t *)malloc(buf_sz);
    uint8_t *dst = (uint8_t *)malloc(buf_sz);
    uint32_t mem_iters = 512;
    uint32_t checksum = 0;
    if (src && dst) {
        for (size_t i = 0; i < buf_sz; i++) src[i] = (uint8_t)(i & 0xFF);
        for (uint32_t it = 0; it < mem_iters; it++) {
            memcpy(dst, src, buf_sz);
            dst[it & 0xFF] ^= (uint8_t)it;
            checksum += dst[(it * 13) & 0x3FFF];
        }
    }
    if (src) free(src);
    if (dst) free(dst);
    uint64_t m1 = liw_timer_ticks();
    uint32_t mem_ms = (uint32_t)((m1 - m0) * 10ULL);
    if (mem_ms == 0) mem_ms = 1;
    uint32_t total_mb = (uint32_t)((buf_sz * mem_iters) / (1024 * 1024));

    printf("[RAM] Copia de %u MB na Heap (chk=%u) concluida em %u ms\n",
           total_mb, checksum, mem_ms);

    /* 3. Pontuacao Geral */
    uint32_t cpu_score = 50000U / (cpu_ms + 10U);
    uint32_t mem_score = (total_mb * 1000U) / (mem_ms + 10U);
    uint32_t total_score = cpu_score + mem_score;

    printf("\n------------------------------------------------\n");
    printf("  Pontuacao CPU : %u pts\n", cpu_score);
    printf("  Pontuacao RAM : %u pts\n", mem_score);
    printf("  LIWUS SCORE   : %u pts\n", total_score);
    printf("------------------------------------------------\n");
    return 0;
}
