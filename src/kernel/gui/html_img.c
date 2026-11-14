#include "gui/html_img.h"
#include "kheap.h"
#include "string.h"
#include "serial.h"

#include "src/extra/libs/png/lodepng.h"
#include "src/extra/libs/sjpg/tjpgd.h"

#define HTML_IMG_MAX_SRC_DIM   4096
#define HTML_IMG_MAX_SRC_PIX   (1800U * 1800U)
#define TJPGD_WORK_BYTES       4096

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void compute_target_dims(uint32_t sw, uint32_t sh, int max_w, int max_h,
                                uint32_t *out_w, uint32_t *out_h) {
    uint64_t tw = sw;
    uint64_t th = sh;

    if (max_w > 0 && tw > (uint64_t)max_w) {
        th = (th * (uint64_t)max_w) / tw;
        tw = (uint64_t)max_w;
        if (th == 0) th = 1;
    }
    if (max_h > 0 && th > (uint64_t)max_h) {
        tw = (tw * (uint64_t)max_h) / th;
        th = (uint64_t)max_h;
        if (tw == 0) tw = 1;
    }

    *out_w = (uint32_t)tw;
    *out_h = (uint32_t)th;
}

/* Reamostra imagem RGBA (src_is_bgra=false) ou BGRA (src_is_bgra=true) para LVGL BGRA32 */
static uint8_t *resample_to_bgra(const uint8_t *src, uint32_t sw, uint32_t sh,
                                 bool src_is_bgra, uint32_t tw, uint32_t th) {
    size_t dst_bytes = (size_t)tw * (size_t)th * 4;
    uint8_t *dst = (uint8_t *)kmalloc(dst_bytes);
    if (!dst) return NULL;

    if (sw == tw && sh == th) {
        if (src_is_bgra) {
            memcpy(dst, src, dst_bytes);
        } else {
            size_t cnt = (size_t)tw * (size_t)th;
            for (size_t i = 0; i < cnt; i++) {
                dst[i * 4 + 0] = src[i * 4 + 2]; /* B */
                dst[i * 4 + 1] = src[i * 4 + 1]; /* G */
                dst[i * 4 + 2] = src[i * 4 + 0]; /* R */
                dst[i * 4 + 3] = src[i * 4 + 3]; /* A */
            }
        }
        return dst;
    }

    for (uint32_t y = 0; y < th; y++) {
        uint32_t sy0 = (uint32_t)(((uint64_t)y * sh) / th);
        uint32_t sy1 = (uint32_t)(((uint64_t)(y + 1) * sh) / th);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > sh) sy1 = sh;

        for (uint32_t x = 0; x < tw; x++) {
            uint32_t sx0 = (uint32_t)(((uint64_t)x * sw) / tw);
            uint32_t sx1 = (uint32_t)(((uint64_t)(x + 1) * sw) / tw);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > sw) sx1 = sw;

            uint32_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0, cnt = 0;
            for (uint32_t sy = sy0; sy < sy1; sy++) {
                const uint8_t *row = src + ((size_t)sy * sw + sx0) * 4;
                for (uint32_t sx = sx0; sx < sx1; sx++) {
                    if (src_is_bgra) {
                        sum_b += row[0];
                        sum_g += row[1];
                        sum_r += row[2];
                        sum_a += row[3];
                    } else {
                        sum_r += row[0];
                        sum_g += row[1];
                        sum_b += row[2];
                        sum_a += row[3];
                    }
                    row += 4;
                    cnt++;
                }
            }
            if (cnt == 0) cnt = 1;
            size_t di = ((size_t)y * tw + x) * 4;
            dst[di + 0] = (uint8_t)(sum_b / cnt);
            dst[di + 1] = (uint8_t)(sum_g / cnt);
            dst[di + 2] = (uint8_t)(sum_r / cnt);
            dst[di + 3] = (uint8_t)(sum_a / cnt);
        }
    }
    return dst;
}

static lv_img_dsc_t *make_img_dsc(uint8_t *bgra, uint32_t w, uint32_t h) {
    lv_img_dsc_t *dsc = (lv_img_dsc_t *)kmalloc(sizeof(lv_img_dsc_t));
    if (!dsc) {
        kfree(bgra);
        return NULL;
    }
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.always_zero = 0;
    dsc->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    dsc->header.w = (lv_coord_t)w;
    dsc->header.h = (lv_coord_t)h;
    dsc->data_size = w * h * 4;
    dsc->data = bgra;
    return dsc;
}

static lv_img_dsc_t *decode_png_bytes(const uint8_t *raw, size_t len, int max_w, int max_h) {
    if (len < 24) return NULL;
    uint32_t pw = read_be32(raw + 16);
    uint32_t ph = read_be32(raw + 20);
    if (pw == 0 || ph == 0 || pw > HTML_IMG_MAX_SRC_DIM || ph > HTML_IMG_MAX_SRC_DIM) {
        return NULL;
    }
    if ((uint64_t)pw * (uint64_t)ph > HTML_IMG_MAX_SRC_PIX) {
        serial_printf("[HTML-IMG] PNG muito grande (%ux%u), pulando\n", pw, ph);
        return NULL;
    }

    uint8_t *rgba = NULL;
    unsigned dw = 0, dh = 0;
    unsigned err = lodepng_decode32(&rgba, &dw, &dh, raw, len);
    if (err || !rgba || dw == 0 || dh == 0) {
        if (rgba) lv_mem_free(rgba);
        return NULL;
    }

    uint32_t tw = dw, th = dh;
    compute_target_dims(dw, dh, max_w, max_h, &tw, &th);
    uint8_t *bgra = resample_to_bgra(rgba, dw, dh, false, tw, th);
    lv_mem_free(rgba);
    if (!bgra) return NULL;

    return make_img_dsc(bgra, tw, th);
}

typedef struct {
    const uint8_t *raw;
    size_t len;
    size_t pos;
    uint8_t *out_bgra;
    uint32_t out_w;
    uint32_t out_h;
} jpg_ctx_t;

static size_t jpg_input_cb(JDEC *jd, uint8_t *buff, size_t ndata) {
    jpg_ctx_t *ctx = (jpg_ctx_t *)jd->device;
    if (!ctx || ctx->pos >= ctx->len) return 0;
    size_t left = ctx->len - ctx->pos;
    size_t take = (ndata < left) ? ndata : left;
    if (buff) {
        memcpy(buff, ctx->raw + ctx->pos, take);
    }
    ctx->pos += take;
    return take;
}

static int jpg_output_cb(JDEC *jd, void *bitmap, JRECT *rect) {
    jpg_ctx_t *ctx = (jpg_ctx_t *)jd->device;
    const uint8_t *rgb = (const uint8_t *)bitmap;
    uint32_t rect_w = (uint32_t)(rect->right - rect->left + 1);

    for (uint32_t y = rect->top; y <= rect->bottom; y++) {
        const uint8_t *row_src = rgb + (size_t)(y - rect->top) * rect_w * 3;
        if (y >= ctx->out_h) continue;
        for (uint32_t x = rect->left; x <= rect->right; x++) {
            if (x >= ctx->out_w) {
                row_src += 3;
                continue;
            }
            size_t di = ((size_t)y * ctx->out_w + x) * 4;
            ctx->out_bgra[di + 0] = row_src[2]; /* B */
            ctx->out_bgra[di + 1] = row_src[1]; /* G */
            ctx->out_bgra[di + 2] = row_src[0]; /* R */
            ctx->out_bgra[di + 3] = 0xFF;       /* A */
            row_src += 3;
        }
    }
    return 1;
}

static lv_img_dsc_t *decode_jpg_bytes(const uint8_t *raw, size_t len, int max_w, int max_h) {
    uint8_t *work = (uint8_t *)kmalloc(TJPGD_WORK_BYTES);
    if (!work) return NULL;

    jpg_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.raw = raw;
    ctx.len = len;

    JDEC jd;
    JRESULT rc = jd_prepare(&jd, jpg_input_cb, work, TJPGD_WORK_BYTES, &ctx);
    if (rc != JDR_OK || jd.width == 0 || jd.height == 0 ||
        jd.width > HTML_IMG_MAX_SRC_DIM || jd.height > HTML_IMG_MAX_SRC_DIM) {
        kfree(work);
        return NULL;
    }

    uint32_t tw = jd.width, th = jd.height;
    compute_target_dims(jd.width, jd.height, max_w, max_h, &tw, &th);

    /* Escolhe maior fator de escala tjpgd (1/1, 1/2, 1/4, 1/8) mantendo >= target e <= limite RAM */
    uint8_t scale = 0;
    while (scale < 3) {
        uint32_t next_w = jd.width >> (scale + 1);
        uint32_t next_h = jd.height >> (scale + 1);
        bool too_big_now = ((uint64_t)(jd.width >> scale) * (jd.height >> scale) > (900U * 900U));
        if ((next_w >= tw && next_h >= th && next_w >= 8 && next_h >= 8) || too_big_now) {
            if (next_w >= 4 && next_h >= 4) {
                scale++;
                continue;
            }
        }
        break;
    }

    uint32_t dw = jd.width >> scale;
    uint32_t dh = jd.height >> scale;
    if (dw == 0) dw = 1;
    if (dh == 0) dh = 1;

    if ((uint64_t)dw * dh > HTML_IMG_MAX_SRC_PIX) {
        kfree(work);
        return NULL;
    }

    uint8_t *decoded = (uint8_t *)kmalloc((size_t)dw * (size_t)dh * 4);
    if (!decoded) {
        kfree(work);
        return NULL;
    }
    memset(decoded, 0xFF, (size_t)dw * (size_t)dh * 4);

    ctx.out_bgra = decoded;
    ctx.out_w = dw;
    ctx.out_h = dh;

    rc = jd_decomp(&jd, jpg_output_cb, scale);
    kfree(work);
    if (rc != JDR_OK) {
        kfree(decoded);
        return NULL;
    }

    if (dw == tw && dh == th) {
        return make_img_dsc(decoded, tw, th);
    }

    compute_target_dims(dw, dh, max_w, max_h, &tw, &th);
    uint8_t *scaled = resample_to_bgra(decoded, dw, dh, true, tw, th);
    kfree(decoded);
    if (!scaled) return NULL;

    return make_img_dsc(scaled, tw, th);
}

lv_img_dsc_t *html_img_decode(const uint8_t *raw, size_t len, int max_w, int max_h) {
    if (!raw || len < 4) return NULL;

    static const uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (len >= 8 && memcmp(raw, png_sig, 8) == 0) {
        return decode_png_bytes(raw, len, max_w, max_h);
    }
    if (raw[0] == 0xFF && raw[1] == 0xD8 && raw[2] == 0xFF) {
        return decode_jpg_bytes(raw, len, max_w, max_h);
    }
    return NULL;
}

static int b64_val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int html_img_decode_data_uri(const char *uri, uint8_t **out_raw, size_t *out_len) {
    if (!uri || !out_raw || !out_len) return -1;
    *out_raw = NULL;
    *out_len = 0;

    if (strncmp(uri, "data:image/", 11) != 0) return -1;
    const char *comma = strchr(uri, ',');
    if (!comma) return -1;

    /* Exige `;base64,` antes da carga */
    bool is_b64 = false;
    for (const char *p = uri + 11; p + 7 <= comma; p++) {
        if (strncmp(p, ";base64", 7) == 0) {
            is_b64 = true;
            break;
        }
    }
    if (!is_b64) return -1;

    const char *src = comma + 1;
    size_t slen = strlen(src);
    if (slen == 0 || slen > 512 * 1024) return -1;

    size_t cap = (slen * 3) / 4 + 4;
    uint8_t *buf = (uint8_t *)kmalloc(cap);
    if (!buf) return -1;

    size_t out_n = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < slen; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '=' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') continue;
        int v = b64_val(ch);
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            buf[out_n++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }

    if (out_n == 0) {
        kfree(buf);
        return -1;
    }
    *out_raw = buf;
    *out_len = out_n;
    return (int)out_n;
}

void html_img_free(lv_img_dsc_t *dsc) {
    if (!dsc) return;
    if (dsc->data) kfree((void *)dsc->data);
    kfree(dsc);
}
