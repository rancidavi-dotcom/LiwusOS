#include "parser.h"

static int unsupported_base(char c) {
    return c == '\'' || c == '"' || c == '\\' ||
           c == ';'  || c == '&' || c == '`'  || c == '$';
}

static int unsupported_legacy(char c) {
    return unsupported_base(c) || c == '|' || c == '<' || c == '>';
}

int liwsh_parse(char *line, size_t capacity, char **argv, size_t argv_capacity) {
    if (!line || !capacity || !argv || argv_capacity < 2)
        return LIWSH_PARSE_INVALID;

    size_t length = 0;
    while (length < capacity && line[length]) {
        if (unsupported_legacy(line[length])) return LIWSH_PARSE_UNSUPPORTED;
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

int liwsh_parse_pipeline(char *line, size_t capacity, liwsh_pipeline_t *pipeline) {
    if (!line || !capacity || !pipeline)
        return LIWSH_PARSE_INVALID;

    size_t length = 0;
    while (length < capacity && line[length]) {
        if (unsupported_base(line[length])) return LIWSH_PARSE_UNSUPPORTED;
        length++;
    }
    if (length == capacity) return LIWSH_PARSE_TOO_LONG;

    for (int s = 0; s < 2; s++) {
        pipeline->stages[s].argc = 0;
        pipeline->stages[s].input_file = NULL;
        pipeline->stages[s].output_file = NULL;
        pipeline->stages[s].append = 0;
        for (int a = 0; a < LIWSH_ARG_MAX; a++) pipeline->stages[s].argv[a] = NULL;
    }
    pipeline->num_stages = 0;

    liwsh_cmd_t *curr = &pipeline->stages[0];
    size_t r = 0;
    size_t w = 0;

    while (r < length) {
        while (r < length && (line[r] == ' ' || line[r] == '\t')) r++;
        if (r == length) break;

        /* Operador PIPE (|) */
        if (line[r] == '|') {
            if (curr->argc == 0) return LIWSH_PARSE_SYNTAX_ERROR;
            if (pipeline->num_stages >= 1) return LIWSH_PARSE_UNSUPPORTED; /* Max 2 estágios */
            pipeline->num_stages = 1;
            curr = &pipeline->stages[1];
            r++;
            continue;
        }

        /* Redirecionamento de entrada (<) */
        if (line[r] == '<') {
            r++;
            while (r < length && (line[r] == ' ' || line[r] == '\t')) r++;
            if (r == length || line[r] == '|' || line[r] == '<' || line[r] == '>')
                return LIWSH_PARSE_SYNTAX_ERROR;

            char *file = &line[w];
            while (r < length && line[r] != ' ' && line[r] != '\t' &&
                   line[r] != '|' && line[r] != '<' && line[r] != '>') {
                line[w++] = line[r++];
            }
            while (r < length && (line[r] == ' ' || line[r] == '\t')) r++;
            line[w++] = '\0';
            curr->input_file = file;
            continue;
        }

        /* Redirecionamento de saída (> ou >>) */
        if (line[r] == '>') {
            int append = 0;
            r++;
            if (r < length && line[r] == '>') {
                append = 1;
                r++;
            }
            while (r < length && (line[r] == ' ' || line[r] == '\t')) r++;
            if (r == length || line[r] == '|' || line[r] == '<' || line[r] == '>')
                return LIWSH_PARSE_SYNTAX_ERROR;

            char *file = &line[w];
            while (r < length && line[r] != ' ' && line[r] != '\t' &&
                   line[r] != '|' && line[r] != '<' && line[r] != '>') {
                line[w++] = line[r++];
            }
            while (r < length && (line[r] == ' ' || line[r] == '\t')) r++;
            line[w++] = '\0';
            curr->output_file = file;
            curr->append = append;
            continue;
        }

        /* Palavra normal (comando ou argumento) */
        if (curr->argc + 1 >= LIWSH_ARG_MAX) return LIWSH_PARSE_TOO_MANY_ARGS;
        char *word = &line[w];
        while (r < length && line[r] != ' ' && line[r] != '\t' &&
               line[r] != '|' && line[r] != '<' && line[r] != '>') {
            line[w++] = line[r++];
        }
        while (r < length && (line[r] == ' ' || line[r] == '\t')) r++;
        line[w++] = '\0';
        curr->argv[curr->argc++] = word;
    }

    if (pipeline->num_stages == 1 && pipeline->stages[1].argc == 0) {
        return LIWSH_PARSE_SYNTAX_ERROR; /* "cmd |" sem segundo comando */
    }

    if (pipeline->stages[0].argc > 0) {
        pipeline->num_stages++;
    }

    return pipeline->num_stages;
}
