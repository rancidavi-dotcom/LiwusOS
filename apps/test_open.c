int open(const char *name, int flags);
int write(int fd, const void *buf, unsigned int count);
void _exit(int status);

void _start(void) {
    char msg[] = "hello\n";
    write(1, msg, 6);
    int fd = open("/a.txt", 0);
    char buf[32];
    int n = 0;
    if (fd >= 0) {
        while ((n = read(fd, buf, 31)) > 0) {
            buf[n] = 0;
            write(1, buf, n);
        }
    } else {
        write(1, "open failed\n", 12);
    }
    write(1, "done\n", 5);
    _exit(0);
}
