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

static int has_lxe_suffix(const char *name) {
    size_t length = text_length(name);
    return length >= 4 && name[length - 4] == '.' &&
           name[length - 3] == 'l' && name[length - 2] == 'x' &&
           name[length - 1] == 'e';
}

static void report_exec_error(const char *name, int error) {
    write_text("liwsh: cannot execute ");
    write_text(name);
    write_text(": ");
    if (error == -LIW_ENOEXEC) write_text("invalid executable\n");
    else if (error == -LIW_EACCES) write_text("permission denied\n");
    else if (error == -LIW_ENOMEM) write_text("out of memory\n");
    else write_text("execution failed\n");
}

static int try_exec(const char *path, char **argv, char **envp) {
    int result = liw_execve(path, argv, envp);
    if (result == -LIW_ENOENT) return 0;
    report_exec_error(argv[0], result);
    liw_exit(126);
}

static void child_exec(char **argv, char **envp) {
    const char *name = argv[0];
    size_t name_length = text_length(name);
    int slash = has_path_separator(name);
    int has_suffix = has_lxe_suffix(name);
    int overflow = 0;
    char candidate[LIWSH_LINE_MAX];

    if (try_exec(name, argv, envp)) return;
    if (!has_suffix) {
        if (name_length + 4 >= LIWSH_LINE_MAX) {
            overflow = 1;
        } else {
            liw_memcpy(candidate, name, name_length);
            liw_memcpy(candidate + name_length, ".lxe", 5);
            if (try_exec(candidate, argv, envp)) return;
        }
    }
    if (!slash) {
        static const char prefix[] = "/bin/";
        size_t prefix_length = sizeof(prefix) - 1;
        if (prefix_length + name_length >= LIWSH_LINE_MAX) {
            overflow = 1;
        } else {
            liw_memcpy(candidate, prefix, prefix_length);
            liw_memcpy(candidate + prefix_length, name, name_length + 1);
            if (try_exec(candidate, argv, envp)) return;
        }
        if (!has_suffix) {
            if (prefix_length + name_length + 4 >= LIWSH_LINE_MAX) {
                overflow = 1;
            } else {
                liw_memcpy(candidate, prefix, prefix_length);
                liw_memcpy(candidate + prefix_length, name, name_length);
                liw_memcpy(candidate + prefix_length + name_length,
                           ".lxe", 5);
                if (try_exec(candidate, argv, envp)) return;
            }
        }
    }
    if (overflow) {
        write_text("liwsh: executable path is too long\n");
        liw_exit(126);
    }
    write_text("liwsh: command not found: ");
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
            write_text("Built-ins: help, version, pwd, cd, ifconfig, ping, exit (power off)\n");
            write_text("Run an application as: program [arguments]\n");
            write_text("Exact paths are tried first; .lxe is the default suffix.\n");
            write_text("Bare names are also searched under /bin.\n");
            write_text("Quotes, escapes, pipes, redirection, and background jobs are unsupported.\n");
        } else if (same(args[0], "version")) {
            write_text("LIWUS Native Shell v0.1 (Native SDK v0.1)\n");
        } else if (same(args[0], "ifconfig")) {
            liw_net_info_t info;
            if (liw_net_info(&info) < 0) {
                write_text("ifconfig: rede nao disponivel\n");
                continue;
            }
            write_text("et0: Link: ");
            write_text(info.link_up ? "UP" : "DOWN");
            write_text(" HWaddr ");
            write_text(info.mac);
            write_text("\n     inet addr: ");
            write_text(info.ip);
            write_text("  Mask: ");
            write_text(info.netmask);
            write_text("  Gateway: ");
            write_text(info.gateway);
            write_text("\n     RX packets: ");
            write_number((int)info.packets_rx);
            write_text(" TX packets: ");
            write_number((int)info.packets_tx);
            write_text("\n");
        } else if (same(args[0], "ping")) {
            if (count < 2) {
                write_text("usage: ping IP [count]\n");
                continue;
            }
            int ping_count = 4;
            if (count >= 3) {
                int c = 0;
                for (const char *p = args[2]; *p >= '0' && *p <= '9'; p++) {
                    c = c * 10 + (*p - '0');
                }
                if (c > 0) ping_count = c;
            }
            liw_net_ping(args[1], (uint32_t)ping_count);
        } else if (same(args[0], "pwd")) {
            if (count != 1) {
                write_text("usage: pwd\n");
                continue;
            }
            char cwd[256];
            if (liw_getcwd(cwd, sizeof(cwd)) < 0) {
                write_text("pwd: cannot read current directory\n");
                continue;
            }
            write_text(cwd);
            write_text("\n");
        } else if (same(args[0], "cd")) {
            if (count != 2) {
                write_text("usage: cd DIRECTORY\n");
                continue;
            }
            if (liw_chdir(args[1]) < 0) {
                write_text("cd: cannot change directory\n");
                continue;
            }
        } else if (same(args[0], "exit")) {
            if (count != 1) {
                write_text("usage: exit\n");
                continue;
            }
            write_text("Shutting down LIWUS...\n");
            if (liw_shutdown() < 0) {
                write_text("liwsh: ACPI poweroff is unavailable\n");
            }
        } else {
            run_program(args, envp);
        }
    }
}
