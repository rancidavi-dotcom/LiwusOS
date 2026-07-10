#include "terminal.h"
#include <stddef.h>

int terminal_parse_line(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    int in_quotes = 0;

    while (*p && argc < TERMINAL_MAX_ARGS - 1) {
        // Skip leading spaces
        while (*p == ' ' && !in_quotes) {
            *p = '\0';
            p++;
        }
        
        if (*p == '\0') break;

        // Handle quotes
        if (*p == '"') {
            in_quotes = !in_quotes;
            *p = '\0';
            p++;
            if (*p == '\0') break;
            argv[argc++] = p;
        } else {
            argv[argc++] = p;
        }

        // Find end of argument
        while (*p) {
            if (*p == '"') {
                in_quotes = !in_quotes;
                *p = '\0';
                p++;
                break;
            } else if (*p == ' ' && !in_quotes) {
                *p = '\0';
                p++;
                break;
            }
            p++;
        }
    }
    argv[argc] = NULL;
    return argc;
}
