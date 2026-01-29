/*
 * gui/gui_main.h
 *
 * Top-level GUI subsystem — initialises all modules in the correct order
 * and provides the compositor kernel task entry point.
 */
#ifndef GUI_MAIN_H
#define GUI_MAIN_H

/* Called from kernel_main() after the VGA framebuffer is known. */
void gui_init(void);

/* Kernel task entry point — loops forever calling compositor_frame(). */
void gui_compositor_task(void);

#endif /* GUI_MAIN_H */
