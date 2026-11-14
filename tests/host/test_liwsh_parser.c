#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_words(const char *input, const char **expected, int count) {
    char line[LIWSH_LINE_MAX];
    char *argv[LIWSH_ARG_MAX];
    size_t length = strlen(input);
    assert(length < sizeof(line));
    memcpy(line, input, length + 1);
    int actual = liwsh_parse(line, sizeof(line), argv, LIWSH_ARG_MAX);
    assert(actual == count);
    for (int i = 0; i < count; i++) assert(strcmp(argv[i], expected[i]) == 0);
    assert(argv[count] == NULL);
}

int main(void) {
    const char *empty[] = {NULL};
    const char *one[] = {"hello.lxe"};
    const char *arguments[] = {"args.lxe", "one", "two", "three"};
    const char *spaced[] = {"external-test.lxe", "demo"};
    expect_words("", empty, 0);
    expect_words(" \t  \t", empty, 0);
    expect_words("hello.lxe", one, 1);
    expect_words("args.lxe one two three", arguments, 4);
    expect_words(" \t external-test.lxe   demo \t", spaced, 2);

    char line[LIWSH_LINE_MAX] = "app 'quoted'";
    char *argv[LIWSH_ARG_MAX];
    assert(liwsh_parse(line, sizeof(line), argv, LIWSH_ARG_MAX) == LIWSH_PARSE_UNSUPPORTED);
    strcpy(line, "app a\\ b");
    assert(liwsh_parse(line, sizeof(line), argv, LIWSH_ARG_MAX) == LIWSH_PARSE_UNSUPPORTED);
    strcpy(line, "app | next");
    assert(liwsh_parse(line, sizeof(line), argv, LIWSH_ARG_MAX) == LIWSH_PARSE_UNSUPPORTED);

    strcpy(line, "a b c");
    assert(liwsh_parse(line, sizeof(line), argv, 3) == LIWSH_PARSE_TOO_MANY_ARGS);
    memset(line, 'x', sizeof(line));
    line[sizeof(line) - 1] = '\0';
    assert(liwsh_parse(line, sizeof(line), argv, LIWSH_ARG_MAX) == 1);
    memset(line, 'x', sizeof(line));
    assert(liwsh_parse(line, sizeof(line), argv, LIWSH_ARG_MAX) == LIWSH_PARSE_TOO_LONG);
    assert(liwsh_parse(NULL, 0, argv, LIWSH_ARG_MAX) == LIWSH_PARSE_INVALID);

    /* Testes da nova pipeline com redirecionamento e pipes */
    liwsh_pipeline_t p;
    strcpy(line, "cat /readme.txt | head");
    int r = liwsh_parse_pipeline(line, sizeof(line), &p);
    assert(r == 2);
    assert(strcmp(p.stages[0].argv[0], "cat") == 0);
    assert(strcmp(p.stages[0].argv[1], "/readme.txt") == 0);
    assert(strcmp(p.stages[1].argv[0], "head") == 0);

    strcpy(line, "ifconfig > /rede.txt");
    assert(liwsh_parse_pipeline(line, sizeof(line), &p) == 1);
    assert(strcmp(p.stages[0].argv[0], "ifconfig") == 0);
    assert(strcmp(p.stages[0].output_file, "/rede.txt") == 0);
    assert(p.stages[0].append == 0);

    strcpy(line, "echo linha >> /log.txt");
    assert(liwsh_parse_pipeline(line, sizeof(line), &p) == 1);
    assert(strcmp(p.stages[0].argv[0], "echo") == 0);
    assert(strcmp(p.stages[0].output_file, "/log.txt") == 0);
    assert(p.stages[0].append == 1);

    strcpy(line, "cat < /in.txt > /out.txt");
    assert(liwsh_parse_pipeline(line, sizeof(line), &p) == 1);
    assert(strcmp(p.stages[0].input_file, "/in.txt") == 0);
    assert(strcmp(p.stages[0].output_file, "/out.txt") == 0);

    puts("LIWSH parser PASS: empty, tokens, whitespace, limits, malformed syntax");
    return 0;
}
