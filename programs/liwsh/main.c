#include <liwus/liwus.h>
#include "parser.h"

static size_t text_length(const char *text) {
    return liw_strlen(text);
}

static void write_text(const char *text) {
    (void)liw_write_all(1, text, text_length(text));
}

static void write_number(int value) {
    char digits[12];
    size_t count = 0;
    unsigned int number = (unsigned int)value;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof(digits));
    while (count) (void)liw_write_all(1, &digits[--count], 1);
}

static int same(const char *left, const char *right) {
    return liw_strcmp(left, right) == 0;
}

static int has_path_separator(const char *name) {
    for (; *name; name++) if (*name == '/') return 1;
    return 0;
}

static void child_exec(char **argv, char **envp) {
    const char *name = argv[0];
    (void)liw_execve(name, argv, envp);

    /* Bare names are searched in the one conventional application directory
     * currently provided by the image; paths containing '/' are exact. */
    if (!has_path_separator(name)) {
        char path[LIWSH_LINE_MAX];
        static const char prefix[] = "/bin/";
        size_t prefix_length = sizeof(prefix) - 1;
        size_t name_length = text_length(name);
        if (prefix_length + name_length < sizeof(path)) {
            liw_memcpy(path, prefix, prefix_length);
            liw_memcpy(path + prefix_length, name, name_length + 1);
            (void)liw_execve(path, argv, envp);
        }
    }

    write_text("liwsh: executable not found or invalid: ");
    write_text(name);
    write_text("\n");
    liw_exit(127);
}

static void run_program(char **argv, char **envp) {
    int child = liw_fork();
    if (child < 0) {
        write_text("liwsh: fork failed\n");
        return;
    }
    if (child == 0) child_exec(argv, envp);

    int status = -1;
    if (liw_waitpid(child, &status, 0) != child) {
        write_text("liwsh: waitpid failed\n");
        return;
    }
    write_text("[liwsh] exit status ");
    write_number(status);
    write_text("\n");
}

/* Returns 1 for a line, 0 for EOF, and -1 for a read error. The kernel's
 * cooked input echoes characters and translates Enter to newline. Ctrl-D is
 * the shell's explicit EOF key when the input line is empty. */
static int read_line(char *line, size_t capacity) {
    size_t length = 0;
    int overflow = 0;
    for (;;) {
        char c;
        long result = liw_read(0, &c, 1);
        if (result < 0) return -1;
        if (result == 0) return length ? 1 : 0;

        if ((unsigned char)c == 4) {
            if (!length && !overflow) return 0;
            c = '\n';
        }
        if (c == '\n' || c == '\r') {
            line[length] = '\0';
            return overflow ? -2 : 1;
        }
        if (c == '\b' || (unsigned char)c == 127) {
            if (length && !overflow) length--;
            continue;
        }
        if ((unsigned char)c < 32 || (unsigned char)c > 126) continue;
        if (length + 1 >= capacity) {
            overflow = 1;
            continue; /* Drain the whole line before showing the next prompt. */
        }
        if (!overflow) line[length++] = c;
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    char line[LIWSH_LINE_MAX];
    char *args[LIWSH_ARG_MAX];

    write_text("LIWUS OS\nNative Shell v0.1\nType help for commands.\n");
    for (;;) {
        write_text("liwus> ");
        int input = read_line(line, sizeof(line));
        if (input == 0) {
            write_text("\n");
            liw_exit(0);
        }
        if (input == -1) {
            write_text("\nliwsh: input read failed\n");
            continue;
        }
        if (input == -2) {
            write_text("\nliwsh: input line too long\n");
            continue;
        }

        int count = liwsh_parse(line, sizeof(line), args, LIWSH_ARG_MAX);
        if (count == 0) continue;
        if (count == LIWSH_PARSE_UNSUPPORTED) {
            write_text("liwsh: quotes, escapes, and shell operators are unsupported\n");
            continue;
        }
        if (count == LIWSH_PARSE_TOO_MANY_ARGS) {
            write_text("liwsh: too many arguments\n");
            continue;
        }
        if (count < 0) {
            write_text("liwsh: invalid command line\n");
            continue;
        }
        if (same(args[0], "help")) {
            write_text("Built-ins: help, version, exit\n");
            write_text("Run an application as: program.lxe [arguments]\n");
            write_text("Bare names are resolved under /bin; paths are exact.\n");
            write_text("Quotes, escapes, pipes, redirection, and background jobs are unsupported.\n");
        } else if (same(args[0], "version")) {
            write_text("LIWUS Native Shell v0.1 (Native SDK v0.1)\n");
        } else if (same(args[0], "exit")) {
            liw_exit(0);
        } else {
            run_program(args, envp);
        }
    }
}
