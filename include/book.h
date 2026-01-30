#ifndef BOOK_H
#define BOOK_H

void init_book_app();
void open_book();
void book_click_handler(int rx, int ry);
struct wl_surface *get_book_surface(); // Forward decl

#endif
