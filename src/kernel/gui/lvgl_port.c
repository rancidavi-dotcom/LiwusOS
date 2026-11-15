#include "lvgl_port.h"
#include "lvgl.h"
#include "vga.h"
#include "mouse.h"
#include "keyboard.h"
#include "kheap.h"
#include "kernel_layout.h"
#include "kernel/kernel_layout.h"

extern void fast_memcpy(void *dst, const void *src, uint64_t size);

#define DISP_BUF_LINES 64
static lv_disp_draw_buf_t g_disp_buf;
static lv_color_t *g_buf1 = NULL;
static lv_disp_drv_t g_disp_drv;
static lv_indev_drv_t g_mouse_drv;
static lv_indev_t *g_mouse_indev = NULL;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t *fb = vga_get_backbuffer();
    if (!fb && vga_is_framebuffer()) {
        extern uint64_t vga_fb_addr;
        fb = (uint32_t *)phys_to_virt(vga_fb_addr);
    }
    if (!fb) {
        lv_disp_flush_ready(drv);
        return;
    }

    uint32_t stride = vga_get_fb_pitch() / 4;
    int32_t w = area->x2 - area->x1 + 1;
    
    for (int32_t y = area->y1; y <= area->y2; y++) {
        uint32_t *dst = fb + y * stride + area->x1;
        fast_memcpy(dst, color_p, w * sizeof(uint32_t));
        color_p += w;
    }

    lv_disp_flush_ready(drv);
}

static void mouse_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    int32_t mx = get_mouse_x();
    int32_t my = get_mouse_y();
    uint32_t sw = vga_get_fb_width();
    uint32_t sh = vga_get_fb_height();

    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= (int32_t)sw) mx = sw - 1;
    if (my >= (int32_t)sh) my = sh - 1;

    data->point.x = mx;
    data->point.y = my;
    data->state = is_left_clicked() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void lvgl_port_init(void) {
    lv_init();

    uint32_t sw = vga_get_fb_width();
    uint32_t sh = vga_get_fb_height();
    if (sw == 0 || sh == 0) {
        sw = 1024;
        sh = 768;
    }

    uint32_t buf_pixels = sw * DISP_BUF_LINES;
    g_buf1 = (lv_color_t *)kmalloc(buf_pixels * sizeof(lv_color_t));
    if (!g_buf1) return;

    lv_disp_draw_buf_init(&g_disp_buf, g_buf1, NULL, buf_pixels);

    lv_disp_drv_init(&g_disp_drv);
    g_disp_drv.hor_res = sw;
    g_disp_drv.ver_res = sh;
    g_disp_drv.flush_cb = disp_flush_cb;
    g_disp_drv.draw_buf = &g_disp_buf;
    lv_disp_drv_register(&g_disp_drv);

    lv_indev_drv_init(&g_mouse_drv);
    g_mouse_drv.type = LV_INDEV_TYPE_POINTER;
    g_mouse_drv.read_cb = mouse_read_cb;
    g_mouse_indev = lv_indev_drv_register(&g_mouse_drv);
}

void lvgl_port_update(void) {
    lv_timer_handler();
}

int lvgl_port_set_resolution(uint16_t width, uint16_t height) {
    if (!width || !height || !g_buf1) return -1;
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp) return -4;
    uint32_t pixels = (uint32_t)width * DISP_BUF_LINES;
    lv_color_t *new_buf = (lv_color_t *)kmalloc(pixels * sizeof(lv_color_t));
    if (!new_buf) return -2;
    if (vga_display_set_mode(width, height) < 0) {
        kfree(new_buf);
        return -3;
    }
    lv_color_t *old_buf = g_buf1;
    g_buf1 = new_buf;
    lv_disp_draw_buf_init(&g_disp_buf, g_buf1, NULL, pixels);
    g_disp_drv.hor_res = width;
    g_disp_drv.ver_res = height;
    g_disp_drv.draw_buf = &g_disp_buf;
    lv_disp_drv_update(disp, &g_disp_drv);
    kfree(old_buf);
    return 0;
}

lv_indev_t *lvgl_get_mouse_indev(void) {
    return g_mouse_indev;
}
