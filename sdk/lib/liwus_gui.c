#include "liwus_gui.h"

// Define syscall macros natively since we don't include kernel internal headers
static inline uint64_t syscall3(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    asm volatile(
        "mov %1, %%rax\n"
        "mov %2, %%rdi\n"
        "mov %3, %%rsi\n"
        "mov %4, %%rdx\n"
        "syscall\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(n), "r"(a1), "r"(a2), "r"(a3)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall2(uint64_t n, uint64_t a1, uint64_t a2) {
    return syscall3(n, a1, a2, 0);
}

static inline uint64_t syscall1(uint64_t n, uint64_t a1) {
    return syscall3(n, a1, 0, 0);
}

Canvas canvas_create(int width, int height, const char* title) {
    return (Canvas)syscall3(120, (uint64_t)width, (uint64_t)height, (uint64_t)title);
}

Node text_create(const char* text) {
    return (Node)syscall2(121, NODE_LABEL, (uint64_t)text);
}

Node button_create(const char* text) {
    return (Node)syscall2(121, NODE_BUTTON, (uint64_t)text);
}

Node panel_create(void) {
    return (Node)syscall2(121, NODE_PANEL, 0);
}

void canvas_add(Canvas canvas, Node child) {
    syscall2(122, (uint64_t)canvas, (uint64_t)child);
}

void node_add_child(Node parent, Node child) {
    syscall2(122, (uint64_t)parent, (uint64_t)child);
}

void node_move(Node node, int x, int y) {
    syscall3(123, (uint64_t)node, (uint64_t)x, (uint64_t)y);
}

void camera_zoom(float zoom) {
    // Pass as integer scaled by 1000
    int z = (int)(zoom * 1000.0f);
    syscall1(124, (uint64_t)z);
}
