#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/unistd.h"

char **environ = 0;

char *getenv(const char *name) {
    if (!name) return NULL;
    if (environ) {
        size_t len = strlen(name);
        for (char **ep = environ; *ep; ep++) {
            if (strncmp(*ep, name, len) == 0 && (*ep)[len] == '=') {
                return *ep + len + 1;
            }
        }
    }
    if (strcmp(name, "TCCDIR") == 0) return "/sys/lib/tcc";
    if (strcmp(name, "PATH") == 0) return "/sys/bin:/bin";
    if (strcmp(name, "HOME") == 0) return "/";
    return NULL;
}

char *realpath(const char *path, char *resolved_path) {
    if (!path) return NULL;
    char *res = resolved_path;
    if (!res) {
        res = (char *)malloc(1024);
        if (!res) return NULL;
    }
    if (path[0] == '/') {
        strncpy(res, path, 1023);
        res[1023] = '\0';
    } else {
        getcwd(res, 1024);
        size_t len = strlen(res);
        if (len > 0 && res[len - 1] != '/') {
            strncat(res, "/", 1023 - len);
            len++;
        }
        strncat(res, path, 1023 - len);
        res[1023] = '\0';
    }
    return res;
}
