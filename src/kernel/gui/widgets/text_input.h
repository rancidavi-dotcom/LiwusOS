#ifndef GUI_TEXT_INPUT_H
#define GUI_TEXT_INPUT_H

#include "../scene/node.h"

#define EDITOR_MAX_FILE_SIZE (64 * 1024)

typedef void (*text_input_change_cb_t)(node_t *input, void *userdata);

node_t *text_input_create(const char *name, int x, int y, int w, int h, const char *initial_text);
void text_input_set_text(node_t *input, const char *text);
const char *text_input_get_text(node_t *input);
void text_input_set_on_change(node_t *input, text_input_change_cb_t cb, void *userdata);
void text_input_set_cursor_pos(node_t *input, uint32_t pos);
void text_input_focus(node_t *input);
void text_input_type_char(node_t *input, uint32_t unicode);

#endif