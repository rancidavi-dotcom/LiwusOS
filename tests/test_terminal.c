#ifdef KERNEL_TEST
#include "framework.h"
#include "terminal.h"
#include "task.h"
#include "sdfs.h"
#include "vfs.h"
#include "string.h"
#include "kheap.h"

static void execute_line(const char *text) {
    char line[TERMINAL_MAX_INPUT];
    char *argv[TERMINAL_MAX_ARGS];
    strncpy(line, text, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    int argc = terminal_parse_line(line, argv);
    terminal_execute(argc, argv);
}

int test_terminal_files(void) {
    char saved_cwd[256];
    strcpy(saved_cwd, current_task->cwd);
    strcpy(current_task->cwd, "/");
    execute_line("mkdir docs");
    execute_line("echo \"hello world\" > docs/readme.txt");
    uint32_t size = 0;
    char *data = sdfs_read_file("/docs/readme.txt", &size);
    int valid = data && size == 12 && memcmp(data, "hello world\n", 12) == 0;
    if (data) kfree(data);
    ASSERT(valid, "mkdir and echo redirection create readable file");
    ASSERT(vfs_open("/docs/readme.txt") != NULL, "cat can open file through VFS");
    execute_line("cat docs/readme.txt");
    execute_line("cd docs");
    execute_line("echo \"second\" >> readme.txt");
    data = sdfs_read_file("/docs/readme.txt", &size);
    valid = data && size == 19 && memcmp(data, "hello world\nsecond\n", 19) == 0;
    if (data) kfree(data);
    ASSERT(valid, "append works relative to current directory");
    execute_line("echo new > readme.txt");
    data = sdfs_read_file("/docs/readme.txt", &size);
    valid = data && size == 4 && memcmp(data, "new\n", 4) == 0;
    if (data) kfree(data);
    ASSERT(valid, "redirection truncates previous contents");
    strcpy(current_task->cwd, saved_cwd);
    sdfs_delete("/docs/readme.txt");
    sdfs_delete("/docs");
    PASS("test_terminal_files");
}
#endif
