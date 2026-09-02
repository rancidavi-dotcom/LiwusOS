#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

#define TERMINAL_MAX_INPUT 256
#define TERMINAL_MAX_ARGS 16

// Terminal Loop
void terminal_task(void);

// Parser
int terminal_parse_line(char *line, char **argv);

// Dispatcher
void terminal_execute(int argc, char **argv);

// Commands
void cmd_help(int argc, char **argv);
void cmd_clear(int argc, char **argv);
void cmd_echo(int argc, char **argv);
void cmd_ls(int argc, char **argv);
void cmd_cd(int argc, char **argv);
void cmd_pwd(int argc, char **argv);
void cmd_cat(int argc, char **argv);
void cmd_mkdir(int argc, char **argv);
void cmd_touch(int argc, char **argv);
void cmd_rm(int argc, char **argv);
void cmd_mv(int argc, char **argv);
void cmd_cp(int argc, char **argv);
void cmd_reboot(int argc, char **argv);
void cmd_version(int argc, char **argv);
void cmd_meminfo(int argc, char **argv);
void cmd_diskinfo(int argc, char **argv);
void cmd_ip(int argc, char **argv);
void cmd_ping(int argc, char **argv);
void cmd_wget(int argc, char **argv);
void cmd_host(int argc, char **argv);
void cmd_tcc(int argc, char **argv);

// Command structure
typedef struct {
    const char *name;
    void (*func)(int argc, char **argv);
    const char *desc;
} terminal_command_t;

#endif
