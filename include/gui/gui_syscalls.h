/*
 * gui/gui_syscalls.h
 *
 * Implementations of GUI syscalls 120-126.
 *
 * These syscalls bridge userspace (Ring 3) applications to the kernel-side
 * LGX compositor scene graph. They can also be invoked safely from Ring 0
 * (kernel tasks), which is how the existing in-kernel GUI apps work today.
 *
 * Handle convention:
 *   - All Canvas and Node values are uint32_t handles.
 *   - Canvas == a window node (NODE_WINDOW) attached to the compositor root.
 *   - Node   == an arbitrary scene-graph node (LABEL/BUTTON/PANEL/IMAGE).
 *   - Handles use node_t.id, which is globally unique in g_scene.
 */
#ifndef GUI_SYSCALLS_H
#define GUI_SYSCALLS_H

#include <stdint.h>
#include <stdbool.h>

/*
 * SYS_GUI_CANVAS_CREATE (120)
 *   Creates a new window canvas with the given size and title, attaches it to
 *   the compositor scene root, and returns its node handle (Canvas).
 *   Args: rdi=width, rsi=height, rdx=title(user ptr|kernel ptr)
 *   Returns: Canvas handle, or 0 on failure.
 */
uint64_t sys_gui_canvas_create(uint64_t width, uint64_t height, const char *title);

/*
 * SYS_GUI_NODE_CREATE (121)
 *   Creates a leaf node of the given type (NODE_LABEL / NODE_BUTTON / NODE_PANEL).
 *   Args: rdi=node_type, rsi=text(user ptr|kernel ptr, optional>
 *   Returns: node handle, or 0 on failure.
 */
uint64_t sys_gui_node_create(uint64_t type, const char *text);

/*
 * SYS_GUI_CANVAS_ADD (122)
 *   Attaches a child node to a parent node (or canvas).
 *   If child already has a parent, it is moved. If node_add_child semantics
 *   require no parent, we first detach.
 *   Args: rdi=parent_handle, rsi=child_handle
 *   Returns: 0 on success, -1 on failure.
 */
uint64_t sys_gui_node_add_child(uint64_t parent_handle, uint64_t child_handle);

/*
 * SYS_GUI_NODE_MOVE (123)
 *   Repositions a node in its parent's (local) coordinate space.
 *   Args: rdi=node_handle, rsi=x, rdx=y
 *   Returns: 0 on success, -1 on failure.
 */
uint64_t sys_gui_node_move(uint64_t node_handle, uint64_t x, uint64_t y);

/*
 * SYS_GUI_CAMERA_ZOOM (124)
 *   Sets the compositor camera zoom. zoom is passed as a fixed-point
 *   integer = zoom * 1000 (matching the sdk lib conv).
 *   Args: rdi=izoom(zoom*1000)
 *   Returns: 0 on success, -1 on failure.
 */
uint64_t sys_gui_camera_zoom(uint64_t izoom);

/*
 * SYS_GUI_IMAGE_CREATE (125)
 *   Creates an NODE_IMAGE inside the given canvas, copying pixel data
 *   (w*h uint32 AARRGGBB) into kernel memory.
 *   Args: rdi=canvas_handle, rsi=width, rdx=height, rcx=buffer(user ptr|kernel ptr)
 *   Returns: image node handle, or 0 on failure.
 */
uint64_t sys_gui_image_create(uint64_t canvas_handle, uint64_t width,
                              uint64_t height, uint32_t *buffer);

/*
 * SYS_GUI_IMAGE_UPDATE (126)
 *   Updates the pixel data of an existing image node.
 *   Args: rdi=image_handle, rsi=buffer, rdx=buffer_size(# of uint32)
 *   Returns: 0 on success, -1 on failure.
 */
uint64_t sys_gui_image_update(uint64_t image_handle, uint32_t *buffer,
                              uint64_t buffer_size);

/*
 * Cleanup callback — invoked from sys_exit_process() for a user-mode task.
 * Destroys every canvas (window) and node the task created, bounding memory
 * and preventing window leaks when a GUI process exits.
 */
void sys_gui_cleanup_task(void);

#endif /* GUI_SYSCALLS_H */
