#ifndef LIWUS_CRAFT_SOFTGL_H
#define LIWUS_CRAFT_SOFTGL_H

int craft_gl_init(int width, int height);
int craft_gl_resize(int width, int height);
void craft_gl_shutdown(void);
void craft_gl_present(void);

#endif
