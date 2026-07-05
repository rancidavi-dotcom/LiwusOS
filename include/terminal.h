#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

void init_terminal_app();
void open_terminal();
void terminal_enable_console_mode(void);
void update_terminal_key(char k);
void terminal_append_output(const char *text);
void terminal_append_output_n(const char *text, int len);
int terminal_has_dirty_output(void);
void terminal_clear_dirty_output(void);
int terminal_needs_update(uint32_t now_ticks);
void terminal_flush_updates(uint32_t now_ticks);
void terminal_submit_line(const char *line);
const char *terminal_get_text_view(uint32_t now_ticks);
int terminal_get_mode(void);
void exec_command_term(const char *cmd_raw);

#endif
