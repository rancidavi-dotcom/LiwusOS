/*
 * src/drivers/image.c
 *
 * Kernel-side image decoding backed by the public-domain stb_image library
 * (vendored at include/drivers/stb_image.h). Runs entirely in ring 0 using
 * the kernel heap (kmalloc/kfree), so it is safe to call from GUI apps.
 *
 * HDR / linear-float paths are compiled out (STBI_NO_HDR / STBI_NO_LINEAR),
 * which also removes the libm dependency (pow/log). Every supported format
 * decodes to plain 8-bit channels, so no SSE is required.
 */

#include "image.h"
#include "kheap.h"
#include "string.h"

#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
/* The kernel has no TLS (FS segment base), so thread-local decoration
   compiles to %fs-relative loads that fault in ring 0. Force plain static. */
#define STBI_NO_THREAD_LOCALS

#define STBI_MALLOC(sz)          kmalloc(sz)
#define STBI_REALLOC(p, new_sz)  krealloc((p), (new_sz))
#define STBI_FREE(p)             kfree(p)
#define STBI_ASSERT(x)           ((void)0)

/* Bound decode sizes to protect the kernel heap from hostile headers. */
#define STBI_MAX_DIMENSIONS      4096

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* stb_image's BMP path uses abs(); the freestanding kernel has no libc. */
int abs(int x) {
    return x < 0 ? -x : x;
}

int image_decode(const uint8_t *data, uint32_t size,
                 uint32_t **out_pixels, int *out_w, int *out_h) {
    if (!data || !out_pixels || !out_w || !out_h)
        return -1;

    int w = 0, h = 0, comp = 0;
    stbi_uc *pixels = stbi_load_from_memory((stbi_uc *)data, (int)size,
                                            &w, &h, &comp, 4);
    if (!pixels)
        return -2; /* unknown / corrupt format */

    /* stbi returns RGBA (R first); the compositor expects ARGB (alpha in
     * the high byte, then R,G,B). Swizzle in place. */
    uint32_t *rgba = (uint32_t *)pixels;
    uint64_t n = (uint64_t)w * (uint64_t)h;
    for (uint64_t i = 0; i < n; i++) {
        uint32_t p = rgba[i];
        uint32_t r = (p >>  0) & 0xFF;
        uint32_t g = (p >>  8) & 0xFF;
        uint32_t b = (p >> 16) & 0xFF;
        uint32_t a = (p >> 24) & 0xFF;
        rgba[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }

    *out_pixels = rgba;
    *out_w = w;
    *out_h = h;
    return 0;
}

void image_free(uint32_t *pixels) {
    if (pixels)
        kfree(pixels);
}
