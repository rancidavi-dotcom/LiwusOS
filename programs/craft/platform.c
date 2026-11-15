#include <stdint.h>
#include <string.h>
#include "platform.h"
#include "softgl.h"
#include "GLFW/glfw3.h"
#include <liwus/syscall.h>

struct GLFWwindow {
    int width, height, close, cursor_mode;
    unsigned char keys[512];
    double cursor_x, cursor_y;
    GLFWkeyfun key_callback;
    GLFWcharfun char_callback;
    GLFWmousebuttonfun mouse_callback;
    GLFWscrollfun scroll_callback;
};
struct GLFWmonitor { int unused; };

static struct GLFWwindow window_storage;
static struct GLFWmonitor primary_monitor;
static GLFWvidmode mode = {320, 200, 8, 8, 8, 60};
static double time_origin;
static uint64_t tick_origin;
static int mouse_buttons[3];
static char clipboard[256];
static int modifiers(void);

static int refresh_window_state(void) {
    liw_window_state_t state = {0};
    if (liw_window_poll(&state) < 0) { window_storage.close = 1; return -1; }
    if (state.closed) window_storage.close = 1;
    if (state.width >= 160 && state.height >= 120 &&
        ((int)state.width != window_storage.width || (int)state.height != window_storage.height)) {
        if (craft_gl_resize((int)state.width, (int)state.height) == 0) {
            window_storage.width = (int)state.width;
            window_storage.height = (int)state.height;
        }
    }
    window_storage.cursor_x = state.mouse_x;
    window_storage.cursor_y = state.mouse_y;
    int now[3] = {state.mouse_left, state.mouse_right, 0};
    for (int i = 0; i < 3; ++i) {
        if (now[i] != mouse_buttons[i] && window_storage.mouse_callback)
            window_storage.mouse_callback(&window_storage, i,
                now[i] ? GLFW_PRESS : GLFW_RELEASE, modifiers());
        mouse_buttons[i] = now[i];
    }
    return 0;
}

uint64_t craft_platform_ticks(void) { return liw_timer_ticks(); }

static int key_from_scancode(uint8_t raw) {
    uint8_t sc = raw & 0x7f;
    static const struct { uint8_t sc; int key; } map[] = {
        {0x01, GLFW_KEY_ESCAPE}, {0x0e, GLFW_KEY_BACKSPACE},
        {0x0f, GLFW_KEY_TAB}, {0x1c, GLFW_KEY_ENTER},
        {0x1d, GLFW_KEY_LEFT_CONTROL}, {0x2a, GLFW_KEY_LEFT_SHIFT},
        {0x36, GLFW_KEY_RIGHT_SHIFT}, {0x39, GLFW_KEY_SPACE},
        {0x3e, GLFW_KEY_F4}, {0x48, GLFW_KEY_UP}, {0x50, GLFW_KEY_DOWN},
        {0x4b, GLFW_KEY_LEFT}, {0x4d, GLFW_KEY_RIGHT},
        {0x02, GLFW_KEY_1}, {0x03, GLFW_KEY_2}, {0x04, GLFW_KEY_3},
        {0x05, GLFW_KEY_4}, {0x06, GLFW_KEY_5}, {0x07, GLFW_KEY_6},
        {0x08, GLFW_KEY_7}, {0x09, GLFW_KEY_8}, {0x0a, GLFW_KEY_9},
        {0x0b, GLFW_KEY_0}, {0x0c, GLFW_KEY_MINUS}, {0x0d, GLFW_KEY_EQUAL},
        {0x10, GLFW_KEY_Q}, {0x11, GLFW_KEY_W}, {0x12, GLFW_KEY_E},
        {0x13, GLFW_KEY_R}, {0x14, GLFW_KEY_T}, {0x15, GLFW_KEY_Y},
        {0x16, GLFW_KEY_U}, {0x17, GLFW_KEY_I}, {0x18, GLFW_KEY_O},
        {0x19, GLFW_KEY_P}, {0x1a, 91}, {0x1b, 93},
        {0x1e, GLFW_KEY_A}, {0x1f, GLFW_KEY_S}, {0x20, GLFW_KEY_D},
        {0x21, GLFW_KEY_F}, {0x22, GLFW_KEY_G}, {0x23, GLFW_KEY_H},
        {0x24, GLFW_KEY_J}, {0x25, GLFW_KEY_K}, {0x26, GLFW_KEY_L},
        {0x27, GLFW_KEY_SEMICOLON}, {0x28, GLFW_KEY_APOSTROPHE},
        {0x29, GLFW_KEY_GRAVE_ACCENT}, {0x2b, 92},
        {0x2c, GLFW_KEY_Z}, {0x2d, GLFW_KEY_X}, {0x2e, GLFW_KEY_C},
        {0x2f, GLFW_KEY_V}, {0x30, GLFW_KEY_B}, {0x31, GLFW_KEY_N},
        {0x32, GLFW_KEY_M}, {0x33, GLFW_KEY_COMMA},
        {0x34, GLFW_KEY_PERIOD}, {0x35, GLFW_KEY_SLASH}
    };
    for (unsigned i = 0; i < sizeof(map)/sizeof(map[0]); ++i)
        if (map[i].sc == sc) return map[i].key;
    return -1;
}

static int modifiers(void) {
    int mods = 0;
    if (window_storage.keys[GLFW_KEY_LEFT_SHIFT] || window_storage.keys[GLFW_KEY_RIGHT_SHIFT]) mods |= GLFW_MOD_SHIFT;
    if (window_storage.keys[GLFW_KEY_LEFT_CONTROL] || window_storage.keys[GLFW_KEY_RIGHT_CONTROL]) mods |= GLFW_MOD_CONTROL;
    return mods;
}

static int ascii_from_key(int key, int shift) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return shift ? key : key + ('a' - 'A');
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        static const char shifted[] = ")!@#$%^&*(";
        return shift ? shifted[key - GLFW_KEY_0] : key;
    }
    if (key == GLFW_KEY_SPACE) return ' ';
    if (key == GLFW_KEY_COMMA) return shift ? '<' : ',';
    if (key == GLFW_KEY_PERIOD) return shift ? '>' : '.';
    if (key == GLFW_KEY_SLASH) return shift ? '?' : '/';
    if (key == GLFW_KEY_APOSTROPHE) return shift ? '"' : '\'';
    if (key == GLFW_KEY_SEMICOLON) return shift ? ':' : ';';
    if (key == GLFW_KEY_MINUS) return shift ? '_' : '-';
    if (key == GLFW_KEY_EQUAL) return shift ? '+' : '=';
    if (key == GLFW_KEY_ENTER) return '\n';
    if (key == GLFW_KEY_TAB) return '\t';
    return 0;
}

int glfwInit(void) {
    memset(&window_storage, 0, sizeof(window_storage));
    memset(mouse_buttons, 0, sizeof(mouse_buttons));
    time_origin = 0;
    tick_origin = craft_platform_ticks();
    return GLFW_TRUE;
}
void glfwTerminate(void) {
    liw_window_grab_mouse(0);
    liw_window_finish();
}
GLFWwindow *glfwCreateWindow(int width, int height, const char *title,
                             GLFWmonitor *monitor, GLFWwindow *share) {
    (void)title; (void)monitor; (void)share;
    liw_window_state_t state = {0};
    if (liw_window_poll(&state) == 0 && state.width >= 160 && state.height >= 120) {
        width = (int)state.width; height = (int)state.height;
    }
    if (width <= 0 || width > 1920) width = 800;
    if (height <= 0 || height > 1080) height = 520;
    window_storage.width = width;
    window_storage.height = height;
    if (craft_gl_init(width, height) != 0) return NULL;
    return &window_storage;
}
GLFWmonitor *glfwGetPrimaryMonitor(void) { return &primary_monitor; }
const GLFWvidmode *glfwGetVideoModes(GLFWmonitor *monitor, int *count) {
    (void)monitor; if (count) *count = 1; return &mode;
}
void glfwMakeContextCurrent(GLFWwindow *w) { (void)w; }
void glfwSwapInterval(int interval) { (void)interval; }
void glfwSwapBuffers(GLFWwindow *w) { (void)w; craft_gl_present(); }
int glfwWindowShouldClose(GLFWwindow *w) { return w ? w->close : 1; }
void glfwSetInputMode(GLFWwindow *w, int mode_id, int value) {
    if (w && mode_id == GLFW_CURSOR) {
        w->cursor_mode = value;
        liw_window_grab_mouse(value == GLFW_CURSOR_DISABLED ? 1 : 0);
    }
}
int glfwGetInputMode(GLFWwindow *w, int mode_id) {
    return w && mode_id == GLFW_CURSOR ? w->cursor_mode : 0;
}
void glfwSetKeyCallback(GLFWwindow *w, GLFWkeyfun cb) { if (w) w->key_callback = cb; }
void glfwSetCharCallback(GLFWwindow *w, GLFWcharfun cb) { if (w) w->char_callback = cb; }
void glfwSetMouseButtonCallback(GLFWwindow *w, GLFWmousebuttonfun cb) { if (w) w->mouse_callback = cb; }
void glfwSetScrollCallback(GLFWwindow *w, GLFWscrollfun cb) { if (w) w->scroll_callback = cb; }
int glfwGetKey(GLFWwindow *w, int key) {
    return w && key >= 0 && key < (int)sizeof(w->keys) ? w->keys[key] : 0;
}
void glfwGetCursorPos(GLFWwindow *w, double *x, double *y) {
    (void)refresh_window_state();
    if (x) *x = w ? w->cursor_x : 0;
    if (y) *y = w ? w->cursor_y : 0;
}
void glfwSetCursorPos(GLFWwindow *w, double x, double y) {
    if (w) {
        w->cursor_x = x;
        w->cursor_y = y;
    }
}
const char *glfwGetClipboardString(GLFWwindow *w) { (void)w; return clipboard; }
void glfwGetWindowSize(GLFWwindow *w, int *width, int *height) {
    (void)refresh_window_state();
    if (width) *width = w ? w->width : 640;
    if (height) *height = w ? w->height : 400;
}
void glfwGetFramebufferSize(GLFWwindow *w, int *width, int *height) {
    (void)refresh_window_state();
    glfwGetWindowSize(w, width, height);
}
double glfwGetTime(void) {
    return time_origin + (craft_platform_ticks() - tick_origin) * 0.01;
}
void glfwSetTime(double value) { time_origin = value; tick_origin = craft_platform_ticks(); }

void glfwPollEvents(void) {
    (void)refresh_window_state();
    liw_key_event_t ev;
    while (liw_key_event(&ev) > 0) {
        int key = key_from_scancode(ev.scancode);
        if (key < 0 || key >= (int)sizeof(window_storage.keys)) continue;
        int pressed = ev.pressed != 0;
        window_storage.keys[key] = (unsigned char)pressed;
        if (window_storage.key_callback)
            window_storage.key_callback(&window_storage, key, ev.scancode,
                pressed ? GLFW_PRESS : GLFW_RELEASE, modifiers());
        if (pressed && window_storage.char_callback) {
            int ch = ascii_from_key(key, (modifiers() & GLFW_MOD_SHIFT) != 0);
            if (ch) window_storage.char_callback(&window_storage, (unsigned int)ch);
        }
    }

}

void craft_platform_present(const uint32_t *pixels, int width, int height) {
    if (pixels && width > 0 && height > 0)
        (void)liw_window_present(pixels, (uint32_t)width, (uint32_t)height);
}
