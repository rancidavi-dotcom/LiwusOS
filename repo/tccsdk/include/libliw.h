#ifndef LIBLIW_H
#define LIBLIW_H

#include <stdint.h>
#include <stddef.h>

// Estruturas de Framebuffer
typedef struct {
    uint32_t *address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
} liw_fb_info_t;

// API Grafica Base
void liw_get_fb_info(liw_fb_info_t *info);
void liw_present_fb(void);
void liw_draw_pixel(int x, int y, uint32_t color);

// API Grafica Avancada
uint32_t *liw_create_buffer(uint32_t w, uint32_t h);
void liw_present_frame(const uint32_t *buffer, uint32_t w, uint32_t h);

// Teclado
int liw_key_down(int key);
int liw_get_key_event(void *ev);

// Sistema
int liw_get_ticks(void);
void print(const char *s);
void print_int(int n);

// Syscalls brutos
int syscall_fork(void);
int syscall_waitpid(int pid, int *status, int options);
void syscall_exit(int status);
uint32_t syscall_brk(uint32_t addr);

#endif
