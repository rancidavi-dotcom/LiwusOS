#ifndef LIWSH_PARSER_H
#define LIWSH_PARSER_H

#include <stddef.h>

#define LIWSH_LINE_MAX 256
#define LIWSH_ARG_MAX  16

enum {
    LIWSH_PARSE_INVALID = -1,
    LIWSH_PARSE_TOO_LONG = -2,
    LIWSH_PARSE_TOO_MANY_ARGS = -3,
    LIWSH_PARSE_UNSUPPORTED = -4
};

/* Tokenizes a bounded, writable line in place. Empty input returns zero. */
int liwsh_parse(char *line, size_t capacity, char **argv, size_t argv_capacity);

#endif
