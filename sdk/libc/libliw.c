#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <libliw.h>

// Declarações das Syscalls
extern int __liw_sys_get_ticks();
extern void __liw_sys_get_fb_info(void* info);
extern void __liw_sys_present_fb();
extern int __liw_sys_key_state(int key);
extern int __liw_sys_get_key_event(void* ev);

int liw_get_ticks() { return __liw_sys_get_ticks(); }
void liw_get_fb_info(liw_fb_info_t* info) { __liw_sys_get_fb_info((void*)info); }
void liw_present_fb() { __liw_sys_present_fb(); }
int liw_key_down(int key) { return __liw_sys_key_state(key); }
int liw_get_key_event(void* ev) { return __liw_sys_get_key_event(ev); }

void liw_draw_pixel(int x, int y, uint32_t color) {
    liw_fb_info_t fb;
    liw_get_fb_info(&fb);
    if (x >= 0 && (uint32_t)x < fb.width && y >= 0 && (uint32_t)y < fb.height) {
        fb.address[y * fb.width + x] = color;
    }
}

void __liw_libc_init() {
    // Inicialização básica
}
