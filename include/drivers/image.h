#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

/*
 * Kernel-side image decoding built on top of the public-domain stb_image
 * library. Supports PNG, JPEG, BMP, GIF, TGA, PSD, PNM, HDR, PIC.
 *
 * Decodes an in-memory image to a 32-bit ARGB (premultiplied not needed:
 * compositor uses a simple alpha test on bit 31..24) pixel buffer.
 *
 * The returned buffer must be released with image_free().
 */

/*
 * Decode `size` bytes of image data into an ARGB pixel buffer.
 * Returns 0 on success and sets out_pixels/w/h, or a negative error
 * code. On success the caller owns *out_pixels and must call image_free().
 */
int image_decode(const uint8_t *data, uint32_t size,
                 uint32_t **out_pixels, int *out_w, int *out_h);

/* Release a buffer returned by image_decode(). */
void image_free(uint32_t *pixels);

#endif /* IMAGE_H */
