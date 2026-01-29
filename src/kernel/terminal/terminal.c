#include "terminal.h"
#include "vga.h"
#include "serial.h"
#include "task.h"

extern int keyboard_pop_char(char *c);
extern int serial_pop_char(char *c);

void terminal_task(void) {
    vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("LiwusOS - Terminal\n");

    char cmd_buffer[TERMINAL_MAX_INPUT];
    int cmd_len = 0;

    while (1) {
        vga_puts("root@liwusos# ");
        serial_print("root@liwusos# ");
        cmd_len = 0;

        while (1) {
            char c;
            if (keyboard_pop_char(&c) || serial_pop_char(&c)) {
                if (c == '\n' || c == '\r') {
                    vga_putc('\n');
                    write_serial('\n');
                    cmd_buffer[cmd_len] = '\0';
                    break;
                } else if (c == '\b' && cmd_len > 0) {
                    cmd_len--;
                    vga_putc('\b');
                    write_serial('\b');
                } else if (c >= 32 && c < 127 && cmd_len < TERMINAL_MAX_INPUT - 1) {
                    cmd_buffer[cmd_len++] = c;
                    vga_putc(c);
                    write_serial(c);
                }
            } else {
                switch_task();
            }
        }

        if (cmd_len == 0) continue;

        char *argv[TERMINAL_MAX_ARGS];
        int argc = terminal_parse_line(cmd_buffer, argv);

        if (argc > 0) {
            terminal_execute(argc, argv);
        }
    }
}
