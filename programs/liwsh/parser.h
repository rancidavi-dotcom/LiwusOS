#ifndef LIWSH_PARSER_H
#define LIWSH_PARSER_H

#include <stddef.h>

#define LIWSH_LINE_MAX 256
#define LIWSH_ARG_MAX  16

enum {
    LIWSH_PARSE_INVALID = -1,
    LIWSH_PARSE_TOO_LONG = -2,
    LIWSH_PARSE_TOO_MANY_ARGS = -3,
    LIWSH_PARSE_UNSUPPORTED = -4,
    LIWSH_PARSE_SYNTAX_ERROR = -5
};

typedef struct {
    char *argv[LIWSH_ARG_MAX];
    int argc;
    char *input_file;   /* < arquivo */
    char *output_file;  /* > ou >> arquivo */
    int append;         /* 1 para >>, 0 para > */
} liwsh_cmd_t;

typedef struct {
    liwsh_cmd_t stages[2];
    int num_stages;
} liwsh_pipeline_t;

/* Tokeniza linha simples em argv (compatibilidade) */
int liwsh_parse(char *line, size_t capacity, char **argv, size_t argv_capacity);

/* Analisa a linha em uma pipeline com suporte a redirecionamento (|, >, >>, <) */
int liwsh_parse_pipeline(char *line, size_t capacity, liwsh_pipeline_t *pipeline);

#endif /* LIWSH_PARSER_H */
