#include "liwus_gui.h"

// Define syscall macros natively since we don't include kernel internal headers
static inline uint64_t syscall3(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    asm volatile(
        "mov %1, %%rax\n"
        "mov %2, %%rdi\n"
        "mov %3, %%rsi\n"
        "mov %4, %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(n), "r"(a1), "r"(a2), "r"(a3)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall4(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    uint64_t ret;
    asm volatile(
        "mov %1, %%rax\n"
        "mov %2, %%rdi\n"
        "mov %3, %%rsi\n"
        "mov %4, %%rdx\n"
        "mov %5, %%rcx\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(n), "r"(a1), "r"(a2), "r"(a3), "r"(a4)
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

Node image_create(Canvas parent, int width, int height, const uint32_t *pixels) {
    // syscall 125: image_create(parent_id, width, height, pixels)
    // Using syscall5 since we need 4 args beyond the syscall number
    uint64_t ret;
    asm volatile(
        "mov $125, %%rax\n"
        "mov %1, %%rdi\n"   // parent
        "mov %2, %%rsi\n"   // width
        "mov %3, %%rdx\n"   // height
        "mov %4, %%r10\n"   // pixels
        "syscall\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"((uint64_t)parent), "r"((uint64_t)width),
          "r"((uint64_t)height), "r"((uint64_t)pixels)
        : "rax", "rdi", "rsi", "rdx", "r10", "rcx", "r11", "memory");
    return (Node)ret;
}

int image_update(Node image, const uint32_t *pixels, uint32_t count) {
    return (int)syscall3(126, (uint64_t)image, (uint64_t)pixels, (uint64_t)count);
}

void camera_zoom(float zoom) {
    int izoom = (int)(zoom * 1000.0f);
    syscall3(124, (uint64_t)izoom, 0, 0);
}

Node image_create(Canvas canvas, int width, int height, uint32_t *buffer) {
    return (Node)syscall4(125, (uint64_t)canvas, (uint64_t)width, (uint64_t)height, (uint64_t)buffer);
}

void image_update(Node image, uint32_t *buffer, int buffer_size) {
    syscall3(126, (uint64_t)image, (uint64_t)buffer, (uint64_t)buffer_size);
}

bool keyboard_is_pressed(uint8_t scancode) {
    return (bool)syscall1(11, (uint64_t)scancode);
}
