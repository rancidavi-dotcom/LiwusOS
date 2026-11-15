#include <liwus/fcntl.h>
#include <liwus/liwus.h>
#include <liwus/stdio.h>
#include <liwus/stdlib.h>
#include <liwus/string.h>
#include <liwus/syscall.h>

#define OUTPUT_LIMIT 65536U

static int contains(const char *text, size_t length, const char *needle) {
    size_t needle_length = strlen(needle);
    if (!needle_length || needle_length > length) return 0;
    for (size_t i = 0; i <= length - needle_length; ++i)
        if (memcmp(text + i, needle, needle_length) == 0) return 1;
    return 0;
}

int main(void) {
    int pipefd[2] = {-1, -1};
    if (liw_pipe(pipefd) < 0) return 10;
    int child = liw_fork();
    if (child < 0) return 11;
    if (child == 0) {
        (void)liw_close(pipefd[0]);
        if (liw_dup2(pipefd[1], 1) != 1) liw_exit(126);
        (void)liw_close(pipefd[1]);
        char *const args[] = {
            "qjs", "--dump-html", "/qjs-ring3.html", NULL
        };
        (void)liw_execve("/bin/qjs.lxe", args, NULL);
        liw_exit(127);
    }

    (void)liw_close(pipefd[1]);
    char output[OUTPUT_LIMIT + 1];
    size_t used = 0;
    int overflow = 0;
    for (;;) {
        char drain[128];
        void *target = overflow ? (void *)drain : (void *)(output + used);
        size_t capacity = overflow ? sizeof(drain) : OUTPUT_LIMIT + 1 - used;
        long count = liw_read(pipefd[0], target, capacity);
        if (count < 0) {
            overflow = 1;
            break;
        }
        if (count == 0) break;
        if (overflow) continue;
        used += (size_t)count;
        if (used > OUTPUT_LIMIT) overflow = 1;
    }
    (void)liw_close(pipefd[0]);

    int child_status = -1;
    if (liw_waitpid(child, &child_status, 0) != child || child_status != 0 ||
        overflow) return 12;
    output[used] = '\0';
    if (!contains(output, used, "<title>Script title</title>") ||
        !contains(output, used, "data-label=\"A &amp; B\"") ||
        !contains(output, used, "data-extra=\"dataset-ok\"") ||
        !contains(output, used, "data-dom-ready=\"yes\"") ||
        !contains(output, used, "data-window-load=\"yes\"") ||
        !contains(output, used, "data-timer=\"7\"") ||
        !contains(output, used, "style=\"background-color: rgb(1, 2, 3)\"") ||
        !contains(output, used, "mutated &lt;&amp;</p>") ||
        !contains(output, used,
                  "id=\"advanced\">DOM_ADVANCED_PASS|WEB_API_PASS|MODULE_PASS</p>") ||
        !contains(output, used, "old &amp; text")) {
        return 13;
    }

    static const char pass[] = "QJS_PIPE_DOM_PASS\n";
    if (liw_write_all(1, pass, sizeof(pass) - 1) < 0) return 14;
    return 0;
}
