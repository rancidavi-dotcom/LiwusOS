#include "parser.h"

static int unsupported(char c) {
    return c == '\'' || c == '"' || c == '\\' || c == '|' || c == '<' ||
           c == '>' || c == ';' || c == '&' || c == '`' || c == '$';
}

int liwsh_parse(char *line, size_t capacity, char **argv, size_t argv_capacity) {
    if (!line || !capacity || !argv || argv_capacity < 2)
        return LIWSH_PARSE_INVALID;

    size_t length = 0;
    while (length < capacity && line[length]) {
        if (unsupported(line[length])) return LIWSH_PARSE_UNSUPPORTED;
        length++;
    }
    if (length == capacity) return LIWSH_PARSE_TOO_LONG;

    size_t read = 0;
    size_t write = 0;
    size_t argc = 0;
    while (read < length) {
        while (read < length && (line[read] == ' ' || line[read] == '\t'))
            read++;
        if (read == length) break;
        if (argc + 1 >= argv_capacity) return LIWSH_PARSE_TOO_MANY_ARGS;

        argv[argc++] = &line[write];
        while (read < length && line[read] != ' ' && line[read] != '\t')
            line[write++] = line[read++];
        while (read < length && (line[read] == ' ' || line[read] == '\t'))
            read++;
        line[write++] = '\0';
    }
    argv[argc] = NULL;
    return (int)argc;
}
