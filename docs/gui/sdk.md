# User-Space SDK — LiwusOS GUI

## Objective

Document the user-space GUI development kit (SDK) that allows applications running on LiwusOS to create windows, buttons, labels, panels, and manage their scene graph nodes through the Scene Graph syscalls (120–124). Covers the public API, syscall implementation, reference application code, and build/deploy workflow.

---

## Problems Solved

- Provides a type-safe C API for user-space applications to access the kernel GUI compositor
- Eliminates the need for per-app framebuffer management, event loops, or draw code — rendering is handled entirely by the kernel compositor
- Bridges the gap between the old LGX framebuffer API (`int $0x80`, syscalls 10–13) and the new Scene Graph model (`syscall` instruction, syscalls 120–124)
- Cross-compilation toolchain reference for LiwusOS target (x86_64-elf)

---

## Architecture

```
User-space Application (e.g., demo_gui)
  │
  ├── liwus_gui.h / liwus_gui.c  (SDK layer)
  │     │
  │     ▼
  │   syscall() instruction  (x86_64: mov rax,rdi,rdx; syscall)
  │     │
  │     ▼
  └── Kernel syscall dispatcher (syscall.c)
        │
        ├── case 120 → sys_gui_canvas_create()
        ├── case 121 → sys_gui_node_create()
        ├── case 122 → sys_gui_canvas_add()
        ├── case 123 → sys_gui_node_move()
        └── case 124 → sys_gui_camera_zoom()
              │
              ▼
        GUI Compositor (kernel task, renders scene graph)
```

---

## Public API — liwus_gui.h

**File:** `sdk/include/liwus_gui.h`

### Types

```c
typedef uint32_t Canvas;   /* handle to a window/canvas node */
typedef uint32_t Node;     /* handle to any scene graph node */
```

### Node Type Constants

```c
#define NODE_GENERIC  0
#define NODE_CANVAS   1
#define NODE_GROUP    2
#define NODE_WINDOW   3
#define NODE_PANEL    4
#define NODE_BUTTON   5
#define NODE_LABEL    6
#define NODE_IMAGE    7
#define NODE_TERMINAL 8
#define NODE_OVERLAY  9
#define NODE_DEBUG    10
```

### Functions

```c
/* Create a new window canvas for the app.
 * The system automatically assigns a red close button that will kill this
 * process when clicked.
 * Parameters:
 *   width  — window width in pixels
 *   height — window height in pixels
 *   title  — null-terminated window title string
 * Returns:
 *   Canvas handle (uint32_t). 0 on failure (OOM or compositor not ready).
 */
Canvas canvas_create(int width, int height, const char* title);

/* Create a text label node.
 * Parameters:
 *   text — null-terminated label text
 * Returns:
 *   Node handle (uint32_t). 0 on failure.
 * Notes:
 *   The node is created at position (0,0) with default size.
 *   Use node_move() to position it.
 */
Node text_create(const char* text);

/* Create a button node.
 * Parameters:
 *   text — null-terminated button label
 * Returns:
 *   Node handle (uint32_t). 0 on failure.
 * Notes:
 *   Default size: 100×30 pixels at position (0,0).
 *   The kernel compositor handles hover/press animation and color changes.
 */
Node button_create(const char* text);

/* Create an empty panel container node.
 * Returns:
 *   Node handle (uint32_t). 0 on failure.
 * Notes:
 *   Default size: 100×100 pixels with semi-transparent dark background (0x88000000).
 */
Node panel_create(void);

/* Add a child node to a canvas (window).
 * Parameters:
 *   canvas — Canvas handle returned by canvas_create()
 *   child  — Node handle to add as a child
 */
void canvas_add(Canvas canvas, Node child);

/* Add a child node to a parent node (hierarchy building).
 * Parameters:
 *   parent — parent node handle
 *   child  — child node handle to add
 * Notes:
 *   A node can only have one parent. Adding a node that already has a parent
 *   will silently fail.
 */
void node_add_child(Node parent, Node child);

/* Move a node to a new (x, y) position within its parent's coordinate space.
 * Parameters:
 *   node — node handle to move
 *   x    — new local X position
 *   y    — new local Y position
 */
void node_move(Node node, int x, int y);

/* Set camera zoom level.
 * Parameters:
 *   zoom — floating-point zoom factor (1.0 = 100%, 2.0 = 200%, 0.5 = 50%)
 * Notes:
 *   The value is internally converted to fixed-point by multiplying by 1000
 *   before passing to the kernel, which further scales by 65536. The kernel
 *   clamps to [0.1×, 8.0×].
 */
void camera_zoom(float zoom);
```

---

## Syscall Implementation — liwus_gui.c

**File:** `sdk/lib/liwus_gui.c`

Uses inline assembly with the `syscall` instruction (x86-64, not `int $0x80`):

```c
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
```

**Syscall number mapping:**

| SDK Function | Syscall | Args In | Kernel Handler |
|---|---|---|---|
| `canvas_create(w,h,title)` | 120 | `rdi=w, rsi=h, rdx=title` | `sys_gui_canvas_create` |
| `text_create(text)` | 121 | `rdi=NODE_LABEL, rsi=text` | `sys_gui_node_create` |
| `button_create(text)` | 121 | `rdi=NODE_BUTTON, rsi=text` | `sys_gui_node_create` |
| `panel_create()` | 121 | `rdi=NODE_PANEL, rsi=0` | `sys_gui_node_create` |
| `canvas_add(canvas,child)` | 122 | `rdi=parent_id, rsi=child_id` | `sys_gui_canvas_add` |
| `node_add_child(parent,child)` | 122 | `rdi=parent_id, rsi=child_id` | `sys_gui_canvas_add` |
| `node_move(node,x,y)` | 123 | `rdi=node_id, rsi=x, rdx=y` | `sys_gui_node_move` |
| `camera_zoom(zoom)` | 124 | `rdi=(int)(zoom*1000)` | `sys_gui_camera_zoom` |

### Integer/Float Conversion for camera_zoom

```c
void camera_zoom(float zoom) {
    int z = (int)(zoom * 1000.0f);
    syscall1(124, (uint64_t)z);
}
```

Kernel side recovers: `float zoom = (float)(regs->rdi) / 1000.0f;`, then sets `camera->zoom_fp = (int)(zoom * 65536.0f)`.

---

## SDK Tools

### liw-builder

**File:** `sdk/tools/liw-builder.c`

A build utility for LiwusOS applications. Handles ELF generation and packaging for the LiwusOS initrd.

```
Usage: liw-builder <input.c> <output.elf>
```

### img-gen

**File:** `sdk/tools/img-gen.c`

Image generation tool for creating raw pixel data arrays in C source files (for wallpapers, sprites, etc.).

```
Usage: img-gen <input.png> <output.c>
```

### Asset Generation Scripts

| Script | Purpose |
|---|---|
| `gen_wallpaper.py` | Generates LiwusOS default wallpaper in C array format |
| `gen_ui_assets.py` | Generates UI sprites (dock icons, close button, etc.) |
| `convert_wallpaper.py` | Converts PNG to LiwusOS raw format |
| `img2c.py` | Converts binary image data to C header array |

---

## Compilation and Build

### Cross-Compiler Toolchain

Target: `x86_64-elf` (x86_64 bare-metal, no OS)

Key components:
- Binutils 2.40+
- GCC 12+ configured with `--target=x86_64-elf`
- Newlib for C standard library (in `sdk/lib/` as pre-built `.a` files)

### Build Command

```bash
x86_64-elf-gcc                                    \
    -ffreestanding                                 \
    -nostdlib                                     \
    -I sdk/include                                 \
    -L sdk/lib                                     \
    apps/demo_gui/demo_gui.c                       \
    -o apps/demo_gui/demo_gui.elf                  \
    -l c -l liwus_gui -l g -l m                   \
    -T sdk/liwus.ld                                \
    -lgcc                                          \
    -Wl,-znoexecstack
```

### Library Dependencies

| Library | Path | Description |
|---|---|---|
| `libc.a` | `sdk/lib/` | Newlib C standard library |
| `liwgui.a` → `liwus_gui.c` | `sdk/lib/` | Scene Graph SDK wrapper |
| `libm.a` | `sdk/lib/` | Math library |
| `libg.a` | TBD | Graphics helper library |
| `libgloss.a` | `sdk/lib/` | Newlib glue layer |
| `libpng.a` | `sdk/lib/` | PNG decode (for view app) |
| `libjpeg.a` | `sdk/lib/` | JPEG decode (for view app) |
| `libz.a` | `sdk/lib/` | zlib compression (for libpng) |

### Linking

The SDK provides no custom linker script for GUI apps alone — reuse the standard LiwusOS linker script (`liwus.ld`), which handles:
- ELF64 executable output
- Loading at the appropriate virtual address
- Proper section alignment for kernel ELF loading

---

## Application Model

### No Event Loop Required

The rendering is handled entirely by the kernel compositor. User-space apps only need to:

1. Create a canvas (window) via `canvas_create()`
2. Create and arrange widgets
3. Stay alive (infinite loop or event wait)
4. The kernel compositor frame loop (`gui_compositor_task()`) renders the scene graph every frame, including all application-owned nodes

```c
// demo_gui.c — minimal app pattern
#include <liwus_gui.h>
#include <libliw.h>

int main() {
    // 1. Create window
    Canvas canvas = canvas_create(400, 300, "App Demo GUI");
    if (!canvas) exit(1);
    
    // 2. Create widgets
    Node title = text_create("User-Space Application!");
    node_move(title, 20, 20);
    
    Node btn = button_create("Native Button");
    node_move(btn, 20, 60);
    
    Node panel = panel_create();
    node_move(panel, 20, 110);
    
    // 3. Add to window
    canvas_add(canvas, title);
    canvas_add(canvas, btn);
    canvas_add(canvas, panel);
    
    // 4. Keep alive — kernel compositor renders
    while (1) {
        for (volatile int i = 0; i < 1000000; i++) {}
    }
    
    return 0;
}
```

### Event Model

Currently, keyboard and mouse events are handled internally by the kernel compositor (event bus → tool system → widgets). Future phases will implement event forwarding to user-space processes via IPC.

---

## Deploying Apps

1. Cross-compile the app to a LiwusOS ELF binary
2. Add the binary to the initrd:
   ```bash
   cp apps/demo_gui/demo_gui.elf isodir/boot/demo_gui.elf
   ```
3. Rebuild the initrd and ISO:
   ```bash
   tar cf initrd.tar -C isodir/boot .
   ./build.sh
   ```
4. Launch with QEMU:
   ```bash
   qemu-system-x86_64 -cdrom liwusos.iso
   ```

---

## Limitations

| Limitation | Description | Future |
|---|---|---|
| No event delivery to user-space | Keyboard/mouse events only processed in kernel widgets | Phase 6 — IPC event channel |
| No input handling in user-space | Apps cannot react to clicks/keys directly | Phase 6 — event forwarding |
| Fixed widget defaults | `button_create` always gives 100×30, `panel_create` always 100×100 | Planned API extension |
| No custom styling | Widgets use kernel theme palette; user-space cannot override | Phase 4 — theme data passed via syscall |
| No `node_destroy` syscall | Nodes cannot be removed from user-space | Phase 5 — add syscall 125 |
| Single canvas per app | One window per process currently | Phase 5 — multi-window support |
| Floating-point in camera_zoom | Requires `-msoft-float` or FPE in kernel | Already handled via fixed-point conversion |

---

## Legacy SDK (libliw.h)

**File:** `sdk/include/libliw.h`

The older framebuffer-based API (still used by `doomgeneric`, `view`):

```c
typedef struct {
    uint32_t *address;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    uint8_t   bpp;
} liw_fb_info_t;

void liw_get_fb_info(liw_fb_info_t *info);    /* syscall 10 */
void liw_present_fb(void);                     /* syscall 11 */
void liw_draw_pixel(int x, int y, uint32_t color); /* syscall 12 */
uint32_t *liw_create_buffer(uint32_t w, uint32_t h);
void liw_present_frame(const uint32_t *buffer, uint32_t w, uint32_t h); /* syscall 13 */
int liw_key_down(int key);
int liw_get_key_event(void *ev);
int liw_get_ticks(void);
```

This API requires user-space apps to manage their own framebuffer and event loop. The Scene Graph SDK (`liwus_gui.h`) is the recommended replacement.

---

## Additional SDK Headers

| Header | Description |
|---|---|
| `sdk/include/liwus_gui.h` | Scene Graph GUI SDK (this document) |
| `sdk/include/libliw.h` | Legacy framebuffer API |
| `sdk/include/stdint.h` | Newlib `stdint.h` |
| `sdk/include/stdio.h` | Newlib `stdio.h` |
| `sdk/include/stdlib.h` | Newlib `stdlib.h` |
| `sdk/include/string.h` | Newlib `string.h` |
| `sdk/include/png.h` | libpng for image loading |
| `sdk/include/setjmp.h` | For libpng error handling |

---

## Dependencies

- **Cross-compiler:** `x86_64-elf-gcc`, `x86_64-elf-binutils`
- **Newlib:** Full C standard library (pre-built, `sdk/lib/libc.a`)
- **Kernel:** Must have GUI subsystem initialized (`gui_init()` called) before any user-space GUI syscall
- **Initrd:** App ELF binaries must be packaged in the initrd

---

## Roadmap

| Feature | Phase | Status |
|---|---|---|
| Basic scene graph syscalls (120–124) | Phase 1 | ✅ Complete |
| Camera zoom from user-space (124) | Phase 1 | ✅ Complete |
| User-space node creation (121) | Phase 1 | ✅ Complete |
| Widget creation helpers (text/button/panel) | Phase 1 | ✅ Complete |
| Event delivery to user-space | Phase 6 | ❌ Not started |
| Multi-canvas apps | Phase 5 | ❌ Not started |
| Custom widget types from user-space | Phase 6 | ❌ Not started |