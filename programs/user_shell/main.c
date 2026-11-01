#include "liwus.h"

#define SHELL_LINE_MAX 256
#define SHELL_ARG_MAX  24
#define SHELL_IO_SIZE  512
#define O_CREAT 0x0200
#define O_TRUNC 0x0400
#define O_APPEND 0x0008
#define SHELL_STAGE_MAX 8

/* This layout is the stable record written by SYS_GETDENTS in syscall.c. */
typedef struct {
    unsigned long long inode;
    long long offset;
    unsigned short record_length;
    unsigned char type;
    char name[256];
} liw_dirent_t;

static unsigned long text_length(const char *s) {
    unsigned long n = 0;
    while (s && s[n]) n++;
    return n;
}

static void write_fd(int fd, const char *text, unsigned long length) {
    while (length) {
        long written = liw_write(fd, text, length);
        if (written <= 0) return;
        text += written;
        length -= (unsigned long)written;
    }
}

static void write_text(const char *text) {
    write_fd(1, text, text_length(text));
}

static void write_number(long value) {
    char digits[24];
    unsigned long n = 0;
    unsigned long magnitude;
    if (value < 0) {
        write_text("-");
        magnitude = (unsigned long)(-(value + 1)) + 1;
    } else {
        magnitude = (unsigned long)value;
    }
    do {
        digits[n++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude && n < sizeof(digits));
    while (n) write_fd(1, &digits[--n], 1);
}

static int read_line(char *line, int capacity) {
    int length = 0;
    while (length < capacity - 1) {
        char c;
        long result = liw_read(0, &c, 1);
        if (result < 0) return -1;
        if (result == 0) continue;
        if (c == '\n' || c == '\r') {
            line[length] = '\0';
            return length;
        }
        if ((unsigned char)c == 3) {
            /* Ctrl+C at the prompt cancels this line; the kernel handles
             * interrupting a foreground child while waitpid is blocked. */
            line[0] = '\0';
            write_text("^C\n");
            return 0;
        }
        if (c == '\b' || (unsigned char)c == 127) {
            if (length) length--;
            continue;
        }
        if ((unsigned char)c >= 32 && (unsigned char)c < 127)
            line[length++] = c;
    }
    line[length] = '\0';
    write_text("shell: input line too long\n");
    return length;
}

static int tokenize(char *line, char **argv) {
    static char operators[SHELL_ARG_MAX][3];
    int argc = 0;
    int operator_count = 0;
    char *read = line;
    char *write = line;
    while (*read && argc < SHELL_ARG_MAX - 1) {
        while (*read == ' ' || *read == '\t') read++;
        if (!*read) break;
        if (*read == '|' || *read == '<' || *read == '>' || *read == '&') {
            char op = *read++;
            operators[operator_count][0] = op;
            operators[operator_count][1] = '\0';
            if (op == '>' && *read == '>') {
                operators[operator_count][1] = '>';
                operators[operator_count][2] = '\0';
                read++;
            }
            argv[argc++] = operators[operator_count++];
            continue;
        }
        argv[argc++] = write;
        char quote = 0;
        while (*read) {
            if (quote) {
                if (*read == quote) {
                    quote = 0;
                    read++;
                } else if (*read == '\\' && read[1] == quote) {
                    *write++ = read[1];
                    read += 2;
                } else {
                    *write++ = *read++;
                }
            } else if (*read == '"' || *read == '\'') {
                quote = *read++;
            } else if (*read == ' ' || *read == '\t' || *read == '|' ||
                       *read == '<' || *read == '>' || *read == '&') {
                break;
            } else {
                *write++ = *read++;
            }
        }
        if (quote) {
            write_text("shell: unmatched quote\n");
            return -1;
        }
        char op = 0;
        char second_op = 0;
        if (*read == '|' || *read == '<' || *read == '>' || *read == '&') {
            op = *read++;
            if (op == '>' && *read == '>') { second_op = '>'; read++; }
        }
        while (*read == ' ' || *read == '\t') read++;
        *write++ = '\0';
        if (op) {
            if (argc >= SHELL_ARG_MAX - 1) {
                write_text("shell: too many arguments\n");
                return -1;
            }
            operators[operator_count][0] = op;
            operators[operator_count][1] = second_op;
            operators[operator_count][2] = '\0';
            argv[argc++] = operators[operator_count++];
        }
    }
    while (*read == ' ' || *read == '\t') read++;
    if (*read) {
        write_text("shell: too many arguments\n");
        return -1;
    }
    argv[argc] = 0;
    return argc;
}

static int same(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

typedef struct {
    int used;
    int id;
    int pid_count;
    long pids[SHELL_STAGE_MAX];
    char command[64];
} shell_job_t;

static shell_job_t shell_jobs[8];

static shell_job_t *find_job(int id) {
    for (int i = 0; i < 8; i++)
        if (shell_jobs[i].used && shell_jobs[i].id == id) return &shell_jobs[i];
    return 0;
}

static shell_job_t *free_job_slot(void) {
    for (int i = 0; i < 8; i++)
        if (!shell_jobs[i].used) return &shell_jobs[i];
    return 0;
}

static void clear_job(shell_job_t *job) {
    if (!job) return;
    for (unsigned long i = 0; i < sizeof(*job); i++)
        ((unsigned char *)job)[i] = 0;
}

static void reap_background_jobs(int announce) {
    for (int i = 0; i < 8; i++) {
        shell_job_t *job = &shell_jobs[i];
        if (!job->used) continue;
        int running = 0;
        for (int p = 0; p < job->pid_count; p++) {
            if (job->pids[p] < 0) continue;
            int status = 0;
            long result = liw_waitpid((int)job->pids[p], &status, 1);
            if (result == job->pids[p] || result < 0) job->pids[p] = -1;
            else running++;
        }
        if (!running) {
            if (announce) {
                write_text("["); write_number(job->id); write_text("] done ");
                write_text(job->command); write_text("\n");
            }
            clear_job(job);
        }
    }
}

static int parse_job_id(const char *text, int *id) {
    if (!text || !*text) return -1;
    int value = 0;
    while (*text) {
        if (*text < '0' || *text > '9') return -1;
        value = value * 10 + (*text++ - '0');
        if (value < 0) return -1;
    }
    *id = value;
    return 0;
}

static void print_jobs(void) {
    reap_background_jobs(1);
    int any = 0;
    for (int i = 0; i < 8; i++) {
        shell_job_t *job = &shell_jobs[i];
        if (!job->used) continue;
        any = 1;
        write_text("["); write_number(job->id); write_text("] running ");
        write_text(job->command); write_text("\n");
    }
    if (!any) write_text("no background jobs\n");
}

static int foreground_job(int id) {
    shell_job_t *job = find_job(id);
    if (!job) { write_text("fg: job not found\n"); return 1; }
    if (liw_job_set_foreground(job->id) < 0) {
        write_text("fg: cannot select job\n");
        return 1;
    }
    write_text("["); write_number(job->id); write_text("] foreground ");
    write_text(job->command); write_text("\n");
    int final_status = 0;
    for (int i = 0; i < job->pid_count; i++) {
        if (job->pids[i] < 0) continue;
        int status = 0;
        long result = liw_waitpid((int)job->pids[i], &status, 0);
        if (result >= 0) final_status = status;
        job->pids[i] = -1;
    }
    liw_job_set_foreground(-1);
    write_text("[job exit "); write_number(final_status); write_text("]\n");
    clear_job(job);
    return final_status;
}

static int builtin_ls(const char *path) {
    char cwd[256];
    if (!path || !*path || (path[0] == '.' && path[1] == '\0')) {
        if (liw_getcwd(cwd, sizeof(cwd)) < 0) {
            write_text("ls: cannot determine current directory\n");
            return 1;
        }
        path = cwd;
    }
    long fd = liw_open(path, 0);
    if (fd < 0) {
        write_text("ls: cannot open directory\n");
        return 1;
    }
    liw_dirent_t entry;
    for (;;) {
        long bytes = liw_getdents((unsigned int)fd, &entry, sizeof(entry));
        if (bytes < 0) {
            write_text("ls: directory read failed\n");
            liw_close((int)fd);
            return 1;
        }
        if (bytes == 0) break;
        write_text(entry.name);
        write_text("\n");
    }
    liw_close((int)fd);
    return 0;
}

static int builtin_cat(int argc, char **argv) {
    char buffer[SHELL_IO_SIZE];
    int status = 0;
    if (argc == 1) {
        for (;;) {
            long count = liw_read(0, buffer, sizeof(buffer));
            if (count < 0) return 1;
            if (count == 0) break;
            write_fd(1, buffer, (unsigned long)count);
        }
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        long fd = liw_open(argv[i], 0);
        if (fd < 0) {
            write_text("cat: cannot open "); write_text(argv[i]); write_text("\n");
            status = 1;
            continue;
        }
        for (;;) {
            long count = liw_read((int)fd, buffer, sizeof(buffer));
            if (count < 0) { status = 1; break; }
            if (count == 0) break;
            write_fd(1, buffer, (unsigned long)count);
        }
        liw_close((int)fd);
    }
    return status;
}

static int builtin_cp(const char *source, const char *destination) {
    long in = liw_open(source, 0);
    if (in < 0) {
        write_text("cp: cannot open source\n");
        return 1;
    }
    long out = liw_open(destination, O_CREAT | O_TRUNC);
    if (out < 0) {
        write_text("cp: cannot create destination\n");
        liw_close((int)in);
        return 1;
    }
    char buffer[SHELL_IO_SIZE];
    int status = 0;
    for (;;) {
        long count = liw_read((int)in, buffer, sizeof(buffer));
        if (count < 0) { status = 1; break; }
        if (count == 0) break;
        long done = 0;
        while (done < count) {
            long written = liw_write((int)out, buffer + done,
                                     (unsigned long)(count - done));
            if (written <= 0) { status = 1; break; }
            done += written;
        }
        if (status) break;
    }
    liw_close((int)in);
    liw_close((int)out);
    if (status) write_text("cp: copy failed\n");
    return status;
}

static int is_builtin(const char *name) {
    return same(name, "help") || same(name, "pwd") || same(name, "cd") ||
           same(name, "ls") || same(name, "cat") || same(name, "mkdir") ||
           same(name, "touch") || same(name, "rm") || same(name, "rmdir") ||
           same(name, "cp") || same(name, "mv") || same(name, "echo");
}

static int run_builtin(int argc, char **argv, int *handled) {
    *handled = 1;
    if (same(argv[0], "help")) {
        write_text("Builtins: help pwd cd ls cat mkdir touch rm rmdir cp mv echo jobs fg\n");
        write_text("Syntax: command [args] [< file] [> file|>> file] [| command] [&]\n");
        write_text("Background jobs: command &, jobs, fg ID. Ctrl+C stops the foreground job.\n");
        write_text("Other commands are loaded as ELF programs by name.\n");
        return 0;
    }
    if (same(argv[0], "pwd")) {
        char cwd[256];
        if (liw_getcwd(cwd, sizeof(cwd)) < 0) {
            write_text("pwd: failed\n");
            return 1;
        }
        write_text(cwd); write_text("\n");
        return 0;
    }
    if (same(argv[0], "cd")) {
        const char *path = argc > 1 ? argv[1] : "/";
        if (liw_chdir(path) < 0) { write_text("cd: directory not found\n"); return 1; }
        return 0;
    }
    if (same(argv[0], "ls")) return builtin_ls(argc > 1 ? argv[1] : ".");
    if (same(argv[0], "cat")) return builtin_cat(argc, argv);
    if (same(argv[0], "mkdir")) {
        if (argc != 2) { write_text("usage: mkdir DIRECTORY\n"); return 2; }
        if (liw_mkdir(argv[1], 0755) < 0) { write_text("mkdir: failed\n"); return 1; }
        return 0;
    }
    if (same(argv[0], "touch")) {
        if (argc != 2) { write_text("usage: touch FILE\n"); return 2; }
        long fd = liw_open(argv[1], O_CREAT);
        if (fd < 0) { write_text("touch: failed\n"); return 1; }
        liw_close((int)fd);
        return 0;
    }
    if (same(argv[0], "rm")) {
        if (argc != 2) { write_text("usage: rm PATH\n"); return 2; }
        if (liw_unlink(argv[1]) < 0 && liw_rmdir(argv[1]) < 0) {
            write_text("rm: failed\n"); return 1;
        }
        return 0;
    }
    if (same(argv[0], "rmdir")) {
        if (argc != 2) { write_text("usage: rmdir DIRECTORY\n"); return 2; }
        if (liw_rmdir(argv[1]) < 0) { write_text("rmdir: failed\n"); return 1; }
        return 0;
    }
    if (same(argv[0], "cp")) {
        if (argc != 3) { write_text("usage: cp SOURCE DESTINATION\n"); return 2; }
        return builtin_cp(argv[1], argv[2]);
    }
    if (same(argv[0], "mv")) {
        if (argc != 3) { write_text("usage: mv SOURCE DESTINATION\n"); return 2; }
        if (liw_rename(argv[1], argv[2]) < 0) {
            write_text("mv: rename failed (destination may already exist)\n");
            return 1;
        }
        return 0;
    }
    if (same(argv[0], "echo")) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) write_text(" ");
            write_text(argv[i]);
        }
        write_text("\n");
        return 0;
    }
    *handled = 0;
    return 0;
}

typedef struct {
    char *argv[SHELL_ARG_MAX];
    int argc;
    char *input_path;
    char *output_path;
    int append;
} shell_stage_t;

static int parse_stages(char **tokens, int token_count,
                        shell_stage_t *stages, int *stage_count) {
    int current = 0;
    for (unsigned long i = 0; i < sizeof(shell_stage_t) * SHELL_STAGE_MAX; i++)
        ((unsigned char *)stages)[i] = 0;
    for (int i = 0; i < token_count; i++) {
        if (same(tokens[i], "|")) {
            if (!stages[current].argc || current + 1 >= SHELL_STAGE_MAX) return -1;
            current++;
        } else if (same(tokens[i], "<") || same(tokens[i], ">") ||
                   same(tokens[i], ">>")) {
            if (++i >= token_count) return -1;
            if (tokens[i - 1][0] == '<') stages[current].input_path = tokens[i];
            else {
                stages[current].output_path = tokens[i];
                stages[current].append = tokens[i - 1][1] == '>';
            }
        } else {
            if (stages[current].argc >= SHELL_ARG_MAX - 1) return -1;
            stages[current].argv[stages[current].argc++] = tokens[i];
        }
    }
    if (!stages[current].argc) return -1;
    *stage_count = current + 1;
    return 0;
}

static int attach_redirections(shell_stage_t *stage) {
    if (stage->input_path) {
        long fd = liw_open(stage->input_path, 0);
        if (fd < 0 || liw_dup2((int)fd, 0) < 0) {
            if (fd >= 0) liw_close((int)fd);
            write_text("shell: cannot open input redirection\n");
            return -1;
        }
        liw_close((int)fd);
    }
    if (stage->output_path) {
        int flags = O_CREAT | (stage->append ? O_APPEND : O_TRUNC);
        long fd = liw_open(stage->output_path, flags);
        if (fd < 0 || liw_dup2((int)fd, 1) < 0) {
            if (fd >= 0) liw_close((int)fd);
            write_text("shell: cannot open output redirection\n");
            return -1;
        }
        liw_close((int)fd);
    }
    return 0;
}

static void run_single_builtin(shell_stage_t *stage) {
    if (attach_redirections(stage) < 0) {
        liw_close(0); liw_close(1);
        return;
    }
    int handled;
    (void)run_builtin(stage->argc, stage->argv, &handled);
    liw_close(0); liw_close(1);
}

static void execute_stages(shell_stage_t *stages, int stage_count, int background) {
    reap_background_jobs(1);
    shell_job_t *job_slot = background ? free_job_slot() : 0;
    if (background && !job_slot) {
        write_text("shell: background job table is full\n");
        return;
    }

    long job_id = liw_job_create();
    if (job_id < 0) {
        write_text("shell: cannot create job\n");
        return;
    }
    long pids[SHELL_STAGE_MAX];
    int pid_count = 0;
    int previous_read = -1;
    int launch_failed = 0;

    for (int i = 0; i < stage_count; i++) {
        int next_pipe[2] = {-1, -1};
        if (i + 1 < stage_count && liw_pipe(next_pipe) < 0) {
            write_text("shell: pipe creation failed\n");
            if (previous_read >= 0) liw_close(previous_read);
            launch_failed = 1;
            break;
        }

        long pid = liw_fork();
        if (pid < 0) {
            write_text("shell: fork failed\n");
            if (previous_read >= 0) liw_close(previous_read);
            if (next_pipe[0] >= 0) liw_close(next_pipe[0]);
            if (next_pipe[1] >= 0) liw_close(next_pipe[1]);
            launch_failed = 1;
            break;
        }
        if (pid == 0) {
            if (previous_read >= 0) liw_dup2(previous_read, 0);
            if (next_pipe[1] >= 0) liw_dup2(next_pipe[1], 1);
            if (attach_redirections(&stages[i]) < 0) liw_exit(126);
            if (previous_read >= 0) liw_close(previous_read);
            if (next_pipe[0] >= 0) liw_close(next_pipe[0]);
            if (next_pipe[1] >= 0) liw_close(next_pipe[1]);

            if (same(stages[i].argv[0], "jobs") || same(stages[i].argv[0], "fg")) {
                write_text("shell: jobs and fg must run as direct shell commands\n");
                liw_exit(2);
            }

            int handled;
            int status = run_builtin(stages[i].argc, stages[i].argv, &handled);
            if (handled) liw_exit(status);
            const char *program = stages[i].argv[0];
            liw_execve(program, stages[i].argv, 0);
            if (!text_length(program) || program[0] == '/') {
                /* Absolute paths are attempted exactly as supplied. */
            } else {
                int has_slash = 0;
                for (const char *p = program; *p; p++) if (*p == '/') has_slash = 1;
                if (!has_slash) {
                    char installed_path[256];
                    const char prefix[] = "/bin/";
                    unsigned long n = sizeof(prefix) - 1;
                    for (unsigned long j = 0; j < n; j++) installed_path[j] = prefix[j];
                    unsigned long j = 0;
                    while (program[j] && n + j < sizeof(installed_path) - 1) {
                        installed_path[n + j] = program[j];
                        j++;
                    }
                    if (!program[j]) {
                        installed_path[n + j] = '\0';
                        liw_execve(installed_path, stages[i].argv, 0);
                    }
                }
            }
            write_text("shell: executable not found or invalid: ");
            write_text(stages[i].argv[0]); write_text("\n");
            liw_exit(127);
        }

        pids[pid_count++] = pid;
        if (previous_read >= 0) liw_close(previous_read);
        if (next_pipe[1] >= 0) liw_close(next_pipe[1]);
        previous_read = next_pipe[0];
    }

    if (previous_read >= 0) liw_close(previous_read);
    if (!pid_count) {
        liw_job_set_foreground(-1);
        return;
    }

    if (background && !launch_failed) {
        job_slot->used = 1;
        job_slot->id = (int)job_id;
        job_slot->pid_count = pid_count;
        for (int i = 0; i < pid_count; i++) job_slot->pids[i] = pids[i];
        unsigned long n = 0;
        const char *name = stages[0].argv[0];
        while (name[n] && n < sizeof(job_slot->command) - 1) {
            job_slot->command[n] = name[n];
            n++;
        }
        job_slot->command[n] = '\0';
        liw_job_set_foreground(-1);
        write_text("["); write_number(job_id); write_text("] started pid ");
        write_number(pids[0]); write_text("\n");
        return;
    }

    if (background && launch_failed)
        write_text("shell: partial job launch; waiting for started processes\n");
    if (liw_job_set_foreground((int)job_id) < 0) {
        write_text("shell: cannot select foreground job\n");
        liw_job_set_foreground(-1);
    }
    int final_status = 0;
    for (int i = 0; i < pid_count; i++) {
        int status = -1;
        if (liw_waitpid((int)pids[i], &status, 0) < 0)
            write_text("shell: waitpid failed\n");
        else
            final_status = status;
    }
    liw_job_set_foreground(-1);
    if (pid_count) {
        write_text("[exit "); write_number(final_status); write_text("]\n");
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    char line[SHELL_LINE_MAX];
    char *tokens[SHELL_ARG_MAX];
    for (;;) {
        reap_background_jobs(1);
        liw_set_foreground(-1);
        char cwd[128];
        if (liw_getcwd(cwd, sizeof(cwd)) < 0) cwd[0] = '\0';
        write_text("liwus:"); write_text(cwd); write_text("$ ");
        int length = read_line(line, sizeof(line));
        if (length <= 0) continue;
        int count = tokenize(line, tokens);
        if (count <= 0) continue;
        int background = same(tokens[count - 1], "&");
        if (background) count--;
        int bad_ampersand = 0;
        for (int i = 0; i < count; i++)
            if (same(tokens[i], "&")) bad_ampersand = 1;
        if (bad_ampersand || count <= 0) {
            write_text("shell: '&' is allowed only at the end of a command\n");
            continue;
        }
        shell_stage_t stages[SHELL_STAGE_MAX];
        int stage_count = 0;
        if (parse_stages(tokens, count, stages, &stage_count) < 0) {
            write_text("shell: invalid command syntax\n");
            continue;
        }
        if (stage_count == 1 &&
            (same(stages[0].argv[0], "jobs") || same(stages[0].argv[0], "fg"))) {
            if (background || stages[0].input_path || stages[0].output_path) {
                write_text("shell: jobs and fg run directly at the prompt\n");
                continue;
            }
            if (same(stages[0].argv[0], "jobs")) {
                if (stages[0].argc != 1) write_text("usage: jobs\n");
                else print_jobs();
            } else {
                int id;
                if (stages[0].argc != 2 || parse_job_id(stages[0].argv[1], &id) < 0)
                    write_text("usage: fg JOB_ID\n");
                else foreground_job(id);
            }
            continue;
        }
        if (!background && stage_count == 1 && is_builtin(stages[0].argv[0])) {
            if (stages[0].input_path || stages[0].output_path) {
                run_single_builtin(&stages[0]);
            } else {
                int handled;
                (void)run_builtin(stages[0].argc, stages[0].argv, &handled);
            }
            continue;
        }
        execute_stages(stages, stage_count, background);
    }
}
